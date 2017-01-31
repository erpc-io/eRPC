#ifndef ERPC_HUGE_ALLOC_H
#define ERPC_HUGE_ALLOC_H

#include <errno.h>
#include <malloc.h>
#include <numaif.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "common.h"
#include "util/rand.h"

namespace ERpc {
/**
 * @brief A hugepage allocator that allows:
 * (a) Allocating and freeing hugepage-backed individual 4K pages. Freed 4K
 *     pages do NOT coalesce to re-form a hugepage chunk.
 * (b) Allocating and freeing multi-hugepage chunks of size >= 2MB. Freed,
 *     possibly multi-hugepage chunks coalesce to re-form larger contiguous
 *     regions.
 *
 * The allocator uses randomly generated positive SHM keys, and deallocates the
 * SHM regions created when it is deleted.
 */
class HugeAllocator {
 private:
  static const size_t kMaxAllocSize = (256 * 1024 * 1024 * 1024ull);

  SlowRand slow_rand;  ///< RNG to generate SHM keys
  size_t numa_node;    ///< NUMA node on which all memory is allocated

  size_t tot_free_hugepages;  ///< Number of free hugepages over all SHM regions

  /**
   * The size of the previous hugepage allocation made internally by this
   * allocator.
   */
  size_t prev_allocation_size;
  size_t tot_memory_reserved;   ///< Total hugepage memory reserved by allocator
  size_t tot_memory_allocated;  ///< Total memory allocated to users

  /// Information about an SHM region
  struct shm_region_t {
    const int key;            ///< The key used to create the SHM region
    const void *alloc_buf;    ///< The start address of the allocated SHM buffer
    const size_t alloc_size;  ///< The size in bytes of the allocated buffer
    const size_t alloc_hugepages;  ///< The number of allocated hugepages

    std::vector<bool> free_hugepage_vec;  /// Bit-vector of free hugepages

    /// For each index in \p free_hugepage_vec, store the number of contiguous
    /// hugepages that were allocated using \p alloc_contiguous. This is
    /// required do free allocated hugepages using just the address.
    std::vector<size_t> nb_contig_vec;

    size_t free_hugepages;  ///< The number of hugepages left in this region

    shm_region_t(int key, void *buf, size_t alloc_size)
        : key(key),
          alloc_buf(buf),
          alloc_size(alloc_size),
          alloc_hugepages(alloc_size / kHugepageSize) {
      assert(alloc_size % kHugepageSize == 0);

      /* Mark all hugepages as free */
      free_hugepage_vec.resize(alloc_hugepages);
      nb_contig_vec.resize(alloc_hugepages);
      for (size_t i = 0; i < alloc_hugepages; i++) {
        free_hugepage_vec[i] = true;
        nb_contig_vec[i] = 0;
      }

      free_hugepages = alloc_hugepages;
    }
  };

  /*
   * SHM regions used by this allocator, in order of increasing allocation-time
   * size.
   */
  std::vector<shm_region_t> shm_list;
  std::vector<void *> page_freelist;  ///< Currently free 4k pages

 public:
  HugeAllocator(size_t initial_size, size_t numa_node);
  ~HugeAllocator();

  /// Allocate a 4K page.
  forceinline void *alloc_page() {
    if (!page_freelist.empty()) {
      /* Use the pages at the back to improve locality */
      void *free_page = page_freelist.back();
      page_freelist.pop_back();

      tot_memory_allocated += kPageSize;
      return free_page;
    } else {
      /* There is no free 4K page. */
      if (tot_free_hugepages == 0) {
        prev_allocation_size *= 2;
        bool success = reserve_hugepages(prev_allocation_size, numa_node);
        if (!success) {
          return nullptr; /* We're out of hugepages */
        }
      }

      /*
       * If we are here, there is at least one SHM region with a free hugepage.
       * Pick the smallest (wrt allocation size) SHM region with a free hugepage
       * and carve it into 4K pages. Note that multiple SHM regions can have
       * free hugepages.
       */
      for (shm_region_t &shm_region : shm_list) {
        if (shm_region.free_hugepages > 0) {
          void *huge_buf = alloc_contiguous(shm_region, 1);
          assert(huge_buf != nullptr); /* 1 hugepages should be available */
          for (size_t i = 0; i < kHugepageSize; i += kPageSize) {
            void *page_addr = (void *)((char *)huge_buf + i);
            page_freelist.push_back(page_addr);
          }

          /* If we are here, we have a free 4K page */
          assert(page_freelist.size() > 0);
          void *free_page = page_freelist.back();
          page_freelist.pop_back();

          tot_memory_allocated += kPageSize;
          return free_page;
        }
      }
    }

    exit(-1); /* We should never get here */
    return nullptr;
  }

  forceinline void free_page(void *page) {
    assert((uintptr_t)page % KB(4) == 0);
    page_freelist.push_back(page);

    tot_memory_allocated -= kPageSize;
  }

  inline size_t get_numa_node() { return numa_node; }

  /// Return the total amount of memory reserved as hugepages.
  inline size_t get_reserved_memory() {
    assert(tot_memory_reserved % kHugepageSize == 0);
    return tot_memory_reserved;
  }

  /// Return the total amount of memory allocated to the user.
  size_t get_allocated_memory() {
    assert(tot_memory_allocated % kPageSize == 0);
    return tot_memory_allocated;
  }

  /// Allocate a hugepage region with at least \p size bytes
  void *alloc_huge(size_t size);

  /// Free a hugepage memory region that was allocated using this allocator
  void free_huge(void *huge_buf);

  /// Print a summary of this allocator
  void print_stats();

 private:
  /**
   * @brief Try to allocate \p num_hugepages contiguous huge pages from \p
   * shm_region. This uses the free hugepages bitvector in the SHM region.
   *
   * @return The address of the allocated buffer if allocation succeeds. NULL
   * if allocation is not possible.
   */
  void *alloc_contiguous(shm_region_t &shm_region, size_t num_hugepages);

  /**
   * @brief Try to reserve \p size (rounded to 2MB) bytes as huge pages on
   * NUMA node \p numa_node.
   *
   * @return True if the allocation succeeds. False if the allocation fails
   * because no more hugepages are available.
   *
   * @throw runtime_error If allocation fails for a reason other than out
   * of memory.
   */
  bool reserve_hugepages(size_t size, size_t numa_node);

  /// Delete the SHM region specified by \p shm_key and \p shm_buf
  void delete_shm(int shm_key, const void *shm_buf);
};
}  // End ERpc

#endif  // ERPC_HUGE_ALLOC_H

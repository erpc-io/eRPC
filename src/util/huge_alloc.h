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
#define class_to_size(c) (4ull * KB(1) * (1ull << (c)))
 private:
  static const size_t kMaxAllocSize = (32 * 1024 * 1024);  ///< 32 MB
  static const size_t kMinInitialSize = kMaxAllocSize;  ///< Need 1 32 MB chunk
  static const size_t kNumClasses = 14;  ///< 4 KB, 8 KB, 16 KB, ..., 32 MB
  static_assert(class_to_size(kNumClasses - 1) == kMaxAllocSize, "");

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
    const int key;      ///< The key used to create the SHM region
    const void *buf;    ///< The start address of the allocated SHM buffer
    const size_t size;  ///< The size in bytes of the allocated buffer

    shm_region_t(int key, void *buf, size_t size)
        : key(key), buf(buf), size(size) {
      assert(size % kHugepageSize == 0);
    }
  };

  /*
   * SHM regions used by this allocator, in order of increasing allocation-time
   * size.
   */
  std::vector<shm_region_t> shm_list;
  std::vector<void *> freelist[kNumClasses];  ///< Per-class freelist

 public:
  HugeAllocator(size_t initial_size, size_t numa_node);
  ~HugeAllocator();

  forceinline size_t get_class(size_t size) {
    size_t size_class = 0;        /* The size class for \p size */
    size_t class_lim = 4 * KB(1); /* The max size for \p size_class */
    while (size > class_lim) {
      size_class++;
      class_lim *= 2;
    }

    return size_class;
  }

  /// Split one chunk from class \p size_class into two chunks of the previous
  /// class, which much be empty.
  forceinline void split(size_t size_class) {
    assert(size_class >= 1);
    assert(!freelist[size_class].empty());
    assert(freelist[size_class - 1].empty());

    void *chunk = freelist[size_class].back();
    freelist[size_class].pop_back();

    size_t class_size = class_to_size(size_class);

    void *chunk_0 = chunk;
    void *chunk_1 = (void *)((char *)chunk + class_size / 2);

    freelist[size_class - 1].push_back(chunk_0);
    freelist[size_class - 1].push_back(chunk_1);
  }

  /// Allocate a chunk from class \p size_class, and add \p size bytes to
  /// the allocated memory.
  forceinline void *alloc_from_class(size_t size_class, size_t size) {
    /* Use the chunks at the back to improve locality */
    void *chunk = freelist[size_class].back();
    freelist[size_class].pop_back();

    tot_memory_allocated += size;
    return chunk;
  }

  /// Special simplified function for allocating 4 KB pages
  forceinline void *alloc_4k() {
    if (!freelist[0].empty()) {
      return alloc_from_class(0, KB(4));
    } else {
      return alloc(KB(4));
    }
  }

  /// Allocate
  forceinline void *alloc(size_t size) {
    if (size >= kMaxAllocSize) {
      throw std::runtime_error("eRPC HugeAllocator: Allocation size too large");
    }

    size_t size_class = get_class(size);
    assert(size_class < kNumClasses);

    if (!freelist[size_class].empty()) {
      return alloc_from_class(size_class, size);
    } else {
      /*
       * There is no free chunk in this class. Find the first larger class with
       * free chunks.
       */
      size_t next_class = size_class + 1;
      for (; next_class < kNumClasses; next_class++) {
        if (!freelist[next_class].empty()) {
          break;
        }
      }

      if (next_class == kNumClasses) {
        /*
         * There's no larger size class with free pages, we we need to allocate
         * more hugepages. This adds some chunks to the largest class.
         */
        prev_allocation_size *= 2;
        bool success = reserve_hugepages(prev_allocation_size, numa_node);
        if (!success) {
          prev_allocation_size /= 2; /* Restore the previous allocation */
          return nullptr;            /* We're out of hugepages */
        } else {
          next_class = kNumClasses - 1;
        }
      }

      /* If we're here, \p next_class has free chunks */
      assert(next_class < kNumClasses);
      while (next_class != size_class) {
        split(next_class);
        next_class--;
      }

      assert(!freelist[size_class].empty());
      return alloc_from_class(size_class, size);
    }

    assert(false);
    exit(-1); /* We should never get here */
    return nullptr;
  }

  forceinline void free(void *chunk, size_t size) {
    size_t size_class = get_class(size);
    freelist[size_class].push_back(chunk);

    tot_memory_allocated -= size;
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

  /// Populate the 4 KB class with at least \p num_chunks chunks.
  bool create_4k_chunk_cache(size_t num_chunks);

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

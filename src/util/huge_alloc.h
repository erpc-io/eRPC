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
#include "transport_types.h"
#include "util/rand.h"

namespace ERpc {

/// Information about an SHM region
struct shm_region_t {
  // Constructor args
  const int shm_key;  ///< The key used to create the SHM region
  const void *buf;    ///< The start address of the allocated SHM buffer
  const size_t size;  ///< The size in bytes of the allocated buffer

  // Filled in by Rpc when this region is registered
  bool registered;          ///< Is this SHM region registered with the NIC?
  MemRegInfo mem_reg_info;  ///< The transport-specific memory registration info

  shm_region_t(int shm_key, void *buf, size_t size, MemRegInfo mem_reg_info)
      : shm_key(shm_key), buf(buf), size(size), mem_reg_info(mem_reg_info) {
    assert(size % kHugepageSize == 0);
  }
};

/**
 * A hugepage allocator that uses per-class freelists. The minimum class size
 * is 4 KB, and class size increases by a factor of 2 until kMaxAllocSize. When
 * a new SHM region is added to the allocator, it is split into chunks of size
 * kMaxAllocSize and added to that class. These chunks are later split to
 * fill up smaller classes.
 *
 * The allocator uses randomly generated positive SHM keys, and deallocates the
 * SHM regions created when it is deleted.
 */
class HugeAllocator {
#define class_to_size(c) (KB(4) * (1ull << (c)))
 private:
  static const size_t kMaxAllocSize = (32 * 1024 * 1024);  ///< 32 MB
  static const size_t kMinInitialSize = kMaxAllocSize;  ///< Need 1 32 MB chunk
  static const size_t kNumClasses = 14;  ///< 4 KB, 8 KB, 16 KB, ..., 32 MB
  static_assert(class_to_size(kNumClasses - 1) == kMaxAllocSize, "");

  std::vector<shm_region_t> shm_list;  ///< SHM regions by increasing alloc size
  std::vector<void *> freelist[kNumClasses];  ///< Per-class freelist

  SlowRand slow_rand;  ///< RNG to generate SHM keys
  size_t numa_node;    ///< NUMA node on which all memory is allocated

  reg_mr_func_t reg_mr_func;
  dereg_mr_func_t dereg_mr_func;

  size_t prev_allocation_size;  ///< Size of previous hugepage reservation
  size_t stat_memory_reserved;  ///< Total hugepage memory reserved by allocator
  size_t stat_memory_allocated;  ///< Total memory allocated to users

 public:
  HugeAllocator(size_t initial_size, size_t numa_node,
                reg_mr_func_t reg_mr_func, dereg_mr_func_t dereg_mr_func);
  ~HugeAllocator();

  /// Special simplified function for allocating 4 KB pages
  forceinline void *alloc_4k() {
    if (!freelist[0].empty()) {
      return alloc_from_class(0, KB(4));
    } else {
      return alloc(KB(4));
    }
  }

  /// Allocate a chunk of with \p size bytes
  forceinline void *alloc(size_t size) {
    if (unlikely(size > kMaxAllocSize)) {
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

  /// Free a chunk
  forceinline void free(void *chunk, size_t size) {
    size_t size_class = get_class(size);
    freelist[size_class].push_back(chunk);

    stat_memory_allocated -= size;
  }

  inline size_t get_numa_node() { return numa_node; }

  /// Return the total amount of memory reserved as hugepages.
  inline size_t get_reserved_memory() {
    assert(stat_memory_reserved % kHugepageSize == 0);
    return stat_memory_reserved;
  }

  /// Return the total amount of memory allocated to the user.
  size_t get_allocated_memory() {
    assert(stat_memory_allocated % kPageSize == 0);
    return stat_memory_allocated;
  }

  /// Populate the 4 KB class with at least \p num_chunks chunks.
  bool create_4k_chunk_cache(size_t num_chunks);

  /// Print a summary of this allocator
  void print_stats();

 private:
  /// Get the class index for a chunk size
  /// XXX: Use inline asm to improve perf
  forceinline size_t get_class(size_t size) {
    assert(size >= 1 && size <= kMaxAllocSize);

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
    assert(size_class < kNumClasses);
    assert(size > (size_class == 0 ? 0 : class_to_size(size_class - 1)) &&
           size <= class_to_size(size_class));

    /* Use the chunks at the back to improve locality */
    void *chunk = freelist[size_class].back();
    freelist[size_class].pop_back();

    stat_memory_allocated += size;
    return chunk;
  }

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

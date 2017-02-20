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
#include "util/buffer.h"
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
 * is kMinClassSize, and class size increases by a factor of 2 until
 * kMaxClassSize.
 *
 * When a new SHM region is added to the allocator, it is split into Buffers of
 * size kMaxClassSize and added to that class. These Buffers are later split to
 * fill up smaller classes.
 *
 * The size of allocated buffers is always equal to a class size, even when
 * a smaller buffer is requested.
 *
 * The allocator uses randomly generated positive SHM keys, and deallocates the
 * SHM regions created when it is deleted.
 */
class HugeAllocator {
 public:
  static const size_t kMinClassSize = 64;     ///< Min allocation size
  static const size_t kMinClassBitShift = 6;  ///< For division by kMinClassSize
  static_assert((kMinClassSize >> kMinClassBitShift) == 1, "");

  static const size_t kMaxClassSize = MB(8);  ///< Max allocation size
  static_assert(kMaxClassSize <= std::numeric_limits<int>::max(), "");

  static const size_t kNumClasses = 18;  ///< 64 B (2^6), ..., 8 MB (2^23)
  static_assert(kMaxClassSize == kMinClassSize << (kNumClasses - 1), "");

  /// Return the maximum size of a class
  static constexpr size_t class_to_size(size_t class_i) {
    return kMinClassSize * (1ull << class_i);
  }

  /**
   * @brief Construct the hugepage allocator
   * @throw \p runtime_error if construction fails
   */
  HugeAllocator(size_t initial_size, size_t numa_node,
                reg_mr_func_t reg_mr_func, dereg_mr_func_t dereg_mr_func);
  ~HugeAllocator();

  /**
   * @brief Allocate a Buffer
   *
   * @param size Minimum size of the allocated Buffer. \p size need not equal
   * a class size, but the allocated Buffer's \p size is a class size.
   *
   * @return The allocated buffer. The buffer is invalid if we ran out of
   * memory.
   *
   * @throw \p runtime_error if \p size is invalid, or if hugepage reservation
   * failure is catastrophic
   */
  inline Buffer alloc(size_t size) {
    if (unlikely(size > kMaxClassSize)) {
      throw std::runtime_error("eRPC HugeAllocator: Allocation size too large");
    }

    size_t size_class = get_class(size);
    assert(size_class < kNumClasses);

    if (!freelist[size_class].empty()) {
      return alloc_from_class(size_class);
    } else {
      /*
       * There is no free Buffer in this class. Find the first larger class with
       * free Buffers.
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
         * more hugepages. This adds some Buffers to the largest class.
         */
        prev_allocation_size *= 2;
        bool success = reserve_hugepages(prev_allocation_size, numa_node);
        if (!success) {
          prev_allocation_size /= 2; /* Restore the previous allocation */
          return Buffer::get_invalid_buffer(); /* We're out of hugepages */
        } else {
          next_class = kNumClasses - 1;
        }
      }

      /* If we're here, \p next_class has free Buffers */
      assert(next_class < kNumClasses);
      while (next_class != size_class) {
        split(next_class);
        next_class--;
      }

      assert(!freelist[size_class].empty());
      return alloc_from_class(size_class);
    }

    assert(false);
    exit(-1); /* We should never get here */
    return Buffer::get_invalid_buffer();
  }

  /// Free a Buffer
  inline void free_buf(Buffer buffer) {
    assert(buffer.is_valid());
    size_t size_class = get_class(buffer.size);
    freelist[size_class].push_back(buffer);
  }

  inline size_t get_numa_node() { return numa_node; }

  /// Return the total amount of memory reserved as hugepages.
  inline size_t get_reserved_memory() {
    assert(stat_memory_reserved % kHugepageSize == 0);
    return stat_memory_reserved;
  }

  /// Create a cache of at lease \p num_buffers Buffers of size at least
  /// \p size. Return true if creation succeeds.
  bool create_cache(size_t size, size_t num_buffers);

  /// Print a summary of this allocator
  void print_stats();

 private:
  /// Get the class index for a Buffer size
  inline size_t get_class(size_t size) {
    assert(size >= 1 && size <= kMaxClassSize);
    /* Use bit shift instead of division to make debug-mode code a faster */
    return msb_index((int)((size - 1) >> kMinClassBitShift));
  }

  /// Reference function for the optimized \p get_class function above
  inline size_t get_class_slow(size_t size) {
    assert(size >= 1 && size <= kMaxClassSize);

    size_t size_class = 0;            /* The size class for \p size */
    size_t class_lim = kMinClassSize; /* The max size for \p size_class */
    while (size > class_lim) {
      size_class++;
      class_lim *= 2;
    }

    return size_class;
  }

  /// Split one Buffers from class \p size_class into two Buffers of the
  /// previous class, which must be an empty class.
  inline void split(size_t size_class) {
    assert(size_class >= 1);
    assert(!freelist[size_class].empty());
    assert(freelist[size_class - 1].empty());

    Buffer &buffer = freelist[size_class].back();
    assert(buffer.size = class_to_size(size_class));

    Buffer buffer_0 = Buffer(buffer.buf, buffer.size / 2, buffer.lkey);
    Buffer buffer_1 = Buffer((void *)((char *)buffer.buf + buffer.size / 2),
                             buffer.size / 2, buffer.lkey);

    freelist[size_class].pop_back(); /* Pop after we don't need the reference */
    freelist[size_class - 1].push_back(buffer_0);
    freelist[size_class - 1].push_back(buffer_1);
  }

  /**
   * @brief Allocate a Buffer from a non-empty class
   * @param size_class Non-empty size class to allocate from
   * @return The allocated Buffer
   */
  inline Buffer alloc_from_class(size_t size_class) {
    assert(size_class < kNumClasses);

    /* Use the Buffers at the back to improve locality */
    Buffer buffer = freelist[size_class].back();
    freelist[size_class].pop_back();

    return buffer;
  }

  /**
   * @brief Try to reserve \p size (rounded to 2MB) bytes as huge pages on
   * \p numa_node.
   *
   * @return True if the allocation succeeds. False if the allocation fails
   * because no more hugepages are available.
   *
   * @throw \p runtime_error if allocation is \a catastrophic (i.e., it fails
   * due to reasons other than out-of-memory).
   */
  bool reserve_hugepages(size_t size, size_t numa_node);

  /// Delete the SHM region specified by \p shm_key and \p shm_buf
  void delete_shm(int shm_key, const void *shm_buf);

  std::vector<shm_region_t> shm_list;  ///< SHM regions by increasing alloc size
  std::vector<Buffer> freelist[kNumClasses];  ///< Per-class freelist

  SlowRand slow_rand;  ///< RNG to generate SHM keys
  size_t numa_node;    ///< NUMA node on which all memory is allocated

  reg_mr_func_t reg_mr_func;
  dereg_mr_func_t dereg_mr_func;

  size_t prev_allocation_size;  ///< Size of previous hugepage reservation
  size_t stat_memory_reserved;  ///< Total hugepage memory reserved by allocator
};
}  // End ERpc

#endif  // ERPC_HUGE_ALLOC_H

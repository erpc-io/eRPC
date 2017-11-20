#ifndef ERPC_HUGE_ALLOC_H
#define ERPC_HUGE_ALLOC_H

#include <errno.h>
#include <malloc.h>
#include <numaif.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdexcept>
#include <vector>

#include "common.h"
#include "transport.h"
#include "util/buffer.h"
#include "util/rand.h"

namespace erpc {

/// Information about an SHM region
struct shm_region_t {
  // Constructor args
  const int shm_key;      /// The key used to create the SHM region
  const uint8_t *buf;     /// The start address of the allocated SHM buffer
  const size_t size;      /// The size in bytes of the allocated SHM buffer
  const bool registered;  /// Is this SHM region registered with the NIC?

  /// The transport-specific memory registration info
  Transport::MemRegInfo mem_reg_info;

  shm_region_t(int shm_key, uint8_t *buf, size_t size, bool registered,
               Transport::MemRegInfo mem_reg_info)
      : shm_key(shm_key),
        buf(buf),
        size(size),
        registered(registered),
        mem_reg_info(mem_reg_info) {
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
 * The \p size field of allocated Buffers equals the requested size, i.e., it's
 * not rounded to the class size.
 *
 * The allocator uses randomly generated positive SHM keys, and deallocates the
 * SHM regions it creates when deleted.
 */
class HugeAlloc {
 public:
  static const size_t kMinClassSize = 64;     /// Min allocation size
  static const size_t kMinClassBitShift = 6;  /// For division by kMinClassSize
  static_assert((kMinClassSize >> kMinClassBitShift) == 1, "");

  static const size_t kMaxClassSize = MB(8);  /// Max allocation size
  static_assert(kMaxClassSize <= std::numeric_limits<int>::max(), "");

  static const size_t kNumClasses = 18;  /// 64 B (2^6), ..., 8 MB (2^23)
  static_assert(kMaxClassSize == kMinClassSize << (kNumClasses - 1), "");

  /// Return the maximum size of a class
  static constexpr size_t class_max_size(size_t class_i) {
    return kMinClassSize * (1ull << class_i);
  }

  /**
   * @brief Construct the hugepage allocator
   * @throw runtime_error if construction fails
   */
  HugeAlloc(size_t initial_size, size_t numa_node,
            Transport::reg_mr_func_t reg_mr_func,
            Transport::dereg_mr_func_t dereg_mr_func);
  ~HugeAlloc();

  /**
   * @brief Allocate memory using raw SHM operations, always bypassing the
   * allocator's freelists. Unlike \p alloc(), the size of the allocated memory
   * need not fit in the allocator's max class size.
   *
   * Optionally, the caller can bypass memory registration. Allocated memory is
   * freed when this allocator is destroyed.
   *
   * This function is used only outside eRPC's core code (e.g., for MICA and
   * testing).
   *
   * @param size The minimum size of the allocated memory
   * @param numa_node The NUMA node to allocate hugepages on
   * @param do_register True iff the hugepages should be registered
   *
   * @return The allocated hugepage buffer, nullptr if we ran out of memory.
   *
   * @throw runtime_error if hugepage reservation failure is catastrophic
   */
  uint8_t *alloc_raw(size_t size, size_t numa_node, bool do_register = false);

  /**
   * @brief Allocate a Buffer using the allocator's freelists, i.e., the max
   * size that can be allocated is the max freelist class size.
   *
   * The actual allocation is done in \p alloc_from_class.
   *
   * @param size The minimum size of the allocated Buffer. \p size need not
   * equal a class size.
   *
   * @return The allocated buffer. The buffer is invalid if we ran out of
   * memory. The \p class_size of the allocated Buffer is equal to a
   * HugeAlloc class size.
   *
   * @throw runtime_error if \p size is too large for the allocator, or if
   * hugepage reservation failure is catastrophic
   */
  inline Buffer alloc(size_t size) {
    assert(size <= kMaxClassSize);

    size_t size_class = get_class(size);
    assert(size_class < kNumClasses);

    if (!freelist[size_class].empty()) {
      return alloc_from_class(size_class);
    } else {
      // There is no free Buffer in this class. Find the first larger class with
      // free Buffers.
      size_t next_class = size_class + 1;
      for (; next_class < kNumClasses; next_class++) {
        if (!freelist[next_class].empty()) break;
      }

      if (next_class == kNumClasses) {
        // There's no larger size class with free pages, we we need to allocate
        // more hugepages. This adds some Buffers to the largest class.
        prev_allocation_size *= 2;
        bool success = reserve_hugepages(prev_allocation_size, numa_node);
        if (!success) {
          prev_allocation_size /= 2;  // Restore the previous allocation
          return Buffer(nullptr, 0, 0);
        } else {
          next_class = kNumClasses - 1;
        }
      }

      // If we're here, \p next_class has free Buffers
      assert(next_class < kNumClasses);
      while (next_class != size_class) {
        split(next_class);
        next_class--;
      }

      assert(!freelist[size_class].empty());
      return alloc_from_class(size_class);
    }

    assert(false);
    exit(-1);  // We should never get here
    return Buffer(nullptr, 0, 0);
  }

  /// Free a Buffer
  inline void free_buf(Buffer buffer) {
    assert(buffer.buf != nullptr);

    size_t size_class = get_class(buffer.class_size);
    assert(class_max_size(size_class) == buffer.class_size);

    freelist[size_class].push_back(buffer);
    stats.user_alloc_tot -= buffer.class_size;
  }

  inline size_t get_numa_node() { return numa_node; }

  /// Return the total amount of memory reserved as hugepages
  inline size_t get_stat_shm_reserved() const {
    assert(stats.shm_reserved % kHugepageSize == 0);
    return stats.shm_reserved;
  }

  /// Return the total amoung of memory allocated to the user
  inline size_t get_stat_user_alloc_tot() const {
    assert(stats.user_alloc_tot % kMinClassSize == 0);
    return stats.user_alloc_tot;
  }

  /// Create a cache of at lease \p num_buffers Buffers of size at least
  /// \p size. Return true if creation succeeds.
  bool create_cache(size_t size, size_t num_buffers);

  /// Print a summary of this allocator
  void print_stats();

 private:
  /**
   * @brief Get the class index for a Buffer size
   * @param size The size of the buffer, which may or may not be a class size
   */
  inline size_t get_class(size_t size) {
    assert(size >= 1 && size <= kMaxClassSize);
    // Use bit shift instead of division to make debug-mode code a faster
    return msb_index(static_cast<int>((size - 1) >> kMinClassBitShift));
  }

  /// Reference function for the optimized \p get_class function above
  inline size_t get_class_slow(size_t size) {
    assert(size >= 1 && size <= kMaxClassSize);

    size_t size_class = 0;             // The size class for \p size
    size_t class_lim = kMinClassSize;  // The max size for \p size_class
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

    Buffer buffer = freelist[size_class].back();
    freelist[size_class].pop_back();
    assert(buffer.class_size == class_max_size(size_class));

    Buffer buffer_0 = Buffer(buffer.buf, buffer.class_size / 2, buffer.lkey);
    Buffer buffer_1 = Buffer(buffer.buf + buffer.class_size / 2,
                             buffer.class_size / 2, buffer.lkey);

    freelist[size_class - 1].push_back(buffer_0);
    freelist[size_class - 1].push_back(buffer_1);
  }

  /**
   * @brief Allocate a Buffer from a non-empty class
   * @param size_class Index of the non-empty size class to allocate from
   */
  inline Buffer alloc_from_class(size_t size_class) {
    assert(size_class < kNumClasses);

    // Use the Buffers at the back to improve locality
    Buffer buffer = freelist[size_class].back();
    assert(buffer.class_size = class_max_size(size_class));
    freelist[size_class].pop_back();

    stats.user_alloc_tot += buffer.class_size;

    return buffer;
  }

  /**
   * @brief Try to reserve \p size (rounded to 2MB) bytes as huge pages on
   * \p numa_node by adding hugepage-backed Buffers to freelists. The
   * allocated hugepages are registered with the NIC.
   *
   * @return True if the allocation succeeds. False if the allocation fails
   * because no more hugepages are available.
   *
   * @throw runtime_error if allocation is \a catastrophic (i.e., it fails
   * due to reasons other than out-of-memory).
   */
  bool reserve_hugepages(size_t size, size_t numa_node);

  /// Delete the SHM region specified by \p shm_key and \p shm_buf
  void delete_shm(int shm_key, const uint8_t *shm_buf);

  std::vector<shm_region_t> shm_list;  /// SHM regions by increasing alloc size
  std::vector<Buffer> freelist[kNumClasses];  /// Per-class freelist

  SlowRand slow_rand;  /// RNG to generate SHM keys
  size_t numa_node;    /// NUMA node on which all memory is allocated

  Transport::reg_mr_func_t reg_mr_func;
  Transport::dereg_mr_func_t dereg_mr_func;

  size_t prev_allocation_size;  /// Size of previous hugepage reservation

  // Stats
  struct {
    size_t shm_reserved = 0;    /// Total hugepage memory reserved by allocator
    size_t user_alloc_tot = 0;  /// Total memory allocated to user
  } stats;
};

}  // End erpc

#endif  // ERPC_HUGE_ALLOC_H

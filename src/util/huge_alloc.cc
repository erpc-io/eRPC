#include "huge_alloc.h"
#include <iostream>
#include "util/logger.h"

#ifdef __linux__
#include <numaif.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

namespace erpc {

HugeAlloc::HugeAlloc(size_t initial_size, size_t numa_node,
                     Transport::reg_mr_func_t reg_mr_func,
                     Transport::dereg_mr_func_t dereg_mr_func)
    : numa_node_(numa_node),
      reg_mr_func_(reg_mr_func),
      dereg_mr_func_(dereg_mr_func) {
  assert(numa_node <= kMaxNumaNodes);

  if (initial_size < k_max_class_size) initial_size = k_max_class_size;
  prev_allocation_size_ = initial_size;
}

HugeAlloc::~HugeAlloc() {
  // Deregister and detach the created SHM regions
  for (shm_region_t &shm_region : shm_list_) {
    if (shm_region.registered_) dereg_mr_func_(shm_region.mem_reg_info_);
#ifdef __linux__
    const int ret =
        shmdt(static_cast<void *>(const_cast<uint8_t *>(shm_region.buf_)));
    if (ret != 0) {
      fprintf(stderr, "HugeAlloc: Error freeing SHM buf for key %d.\n",
              shm_region.shm_key_);
      exit(-1);
    }
#else
    rt_assert(false, "Not implemented on Windows yet");
#endif
  }
}

void HugeAlloc::print_stats() {
  fprintf(stderr, "eRPC HugeAlloc stats:\n");
  fprintf(stderr, "Total reserved SHM = %zu bytes (%.2f MB)\n",
          stats_.shm_reserved_, 1.0 * stats_.shm_reserved_ / MB(1));
  fprintf(stderr, "Total memory allocated to user = %zu bytes (%.2f MB)\n",
          stats_.user_alloc_tot_, 1.0 * stats_.user_alloc_tot_ / MB(1));

  fprintf(stderr, "%zu SHM regions\n", shm_list_.size());
  size_t shm_region_index = 0;
  for (shm_region_t &shm_region : shm_list_) {
    fprintf(stderr, "Region %zu, size %zu MB\n", shm_region_index,
            shm_region.size_ / MB(1));
    shm_region_index++;
  }

  fprintf(stderr, "Size classes:\n");
  for (size_t i = 0; i < k_num_classes; i++) {
    size_t class_size = class_max_size(i);
    if (class_size < KB(1)) {
      fprintf(stderr, "\t%zu B: %zu Buffers\n", class_size,
              freelist_[i].size());
    } else if (class_size < MB(1)) {
      fprintf(stderr, "\t%zu KB: %zu Buffers\n", class_size / KB(1),
              freelist_[i].size());
    } else {
      fprintf(stderr, "\t%zu MB: %zu Buffers\n", class_size / MB(1),
              freelist_[i].size());
    }
  }
}

Buffer HugeAlloc::alloc_raw(size_t size, DoRegister do_register) {
#ifdef __linux__
  std::ostringstream xmsg;  // The exception message
  size = round_up<kHugepageSize>(size);
  int shm_key, shm_id;

  while (true) {
    // Choose a positive SHM key. Negative is fine but it looks scary in the
    // error message.
    shm_key = static_cast<int>(slow_rand_.next_u64());
    shm_key = std::abs(shm_key);

    // Try to get an SHM region
    shm_id = shmget(shm_key, size, IPC_CREAT | IPC_EXCL | 0666 | SHM_HUGETLB);

    if (shm_id == -1) {
      switch (errno) {
        case EEXIST: continue;  // shm_key already exists. Try again.

        case EACCES:
          xmsg << "eRPC HugeAlloc: SHM allocation error. "
               << "Insufficient permissions.";
          throw std::runtime_error(xmsg.str());

        case EINVAL:
          xmsg << "eRPC HugeAlloc: SHM allocation error: SHMMAX/SHMIN "
               << "mismatch. size = " << std::to_string(size) << " ("
               << std::to_string(size / MB(1)) << " MB).";
          throw std::runtime_error(xmsg.str());

        case ENOMEM:
          // Out of memory - this is OK
          ERPC_WARN(
              "eRPC HugeAlloc: Insufficient hugepages. Can't reserve %lu MB.\n",
              size / MB(1));
          return Buffer(nullptr, 0, 0);

        default:
          xmsg << "eRPC HugeAlloc: Unexpected SHM malloc error "
               << strerror(errno);
          throw std::runtime_error(xmsg.str());
      }
    } else {
      // shm_key worked. Break out of the while loop.
      break;
    }
  }

  uint8_t *shm_buf = static_cast<uint8_t *>(shmat(shm_id, nullptr, 0));
  rt_assert(shm_buf != nullptr,
            "eRPC HugeAlloc: shmat() failed. Key = " + std::to_string(shm_key));

  // Mark the SHM region for deletion when this process exits
  shmctl(shm_id, IPC_RMID, nullptr);

  // Bind the buffer to the NUMA node
  const unsigned long nodemask =
      (1ul << static_cast<unsigned long>(numa_node_));
  long ret = mbind(shm_buf, size, MPOL_BIND, &nodemask, 32, 0);
  rt_assert(ret == 0,
            "eRPC HugeAlloc: mbind() failed. Key " + std::to_string(shm_key));

  // If we are here, the allocation succeeded.  Register if needed.
  bool do_register_bool = (do_register == DoRegister::kTrue);
  Transport::mem_reg_info reg_info;
  if (do_register_bool) reg_info = reg_mr_func_(shm_buf, size);

  // Save the SHM region so we can free it later
  shm_list_.push_back(
      shm_region_t(shm_key, shm_buf, size, do_register_bool, reg_info));
  stats_.shm_reserved_ += size;

  // buffer.class_size is invalid because we didn't allocate from a class
  return Buffer(shm_buf, SIZE_MAX,
                do_register_bool ? reg_info.lkey_ : UINT32_MAX);
#else
  _unused(do_register);
  uint8_t *buf = new uint8_t[size];
  return Buffer(buf, SIZE_MAX, UINT32_MAX);
#endif
}

Buffer HugeAlloc::alloc(size_t size) {
  assert(size <= k_max_class_size);

  size_t size_class = get_class(size);
  assert(size_class < k_num_classes);

  if (!freelist_[size_class].empty()) {
    return alloc_from_class(size_class);
  } else {
    // There is no free Buffer in this class. Find the first larger class with
    // free Buffers.
    size_t next_class = size_class + 1;
    for (; next_class < k_num_classes; next_class++) {
      if (!freelist_[next_class].empty()) break;
    }

    if (next_class == k_num_classes) {
      // There's no larger size class with free pages, we we need to allocate
      // more hugepages. This adds some Buffers to the largest class.
      prev_allocation_size_ *= 2;
      bool success = reserve_hugepages(prev_allocation_size_);
      if (!success) {
        prev_allocation_size_ /= 2;  // Restore the previous allocation
        return Buffer(nullptr, 0, 0);
      } else {
        next_class = k_num_classes - 1;
      }
    }

    // If we're here, \p next_class has free Buffers
    assert(next_class < k_num_classes);
    while (next_class != size_class) {
      split(next_class);
      next_class--;
    }

    assert(!freelist_[size_class].empty());
    return alloc_from_class(size_class);
  }

  assert(false);
  exit(-1);  // We should never get here
  return Buffer(nullptr, 0, 0);
}

bool HugeAlloc::reserve_hugepages(size_t size) {
  assert(size >= k_max_class_size);  // We need at least one max-sized buffer
  Buffer buffer = alloc_raw(size, DoRegister::kTrue);
  if (buffer.buf_ == nullptr) return false;

  // Add Buffers to the largest class
  size_t num_buffers = size / k_max_class_size;
  assert(num_buffers >= 1);
  for (size_t i = 0; i < num_buffers; i++) {
    uint8_t *buf = buffer.buf_ + (i * k_max_class_size);
    uint32_t lkey = buffer.lkey_;

    freelist_[k_num_classes - 1].push_back(Buffer(buf, k_max_class_size, lkey));
  }

  return true;
}

}  // namespace erpc

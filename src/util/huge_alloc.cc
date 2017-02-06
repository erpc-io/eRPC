#include "huge_alloc.h"
#include <iostream>

namespace ERpc {

HugeAllocator::HugeAllocator(size_t initial_size, size_t numa_node,
                             reg_mr_func_t reg_mr_func,
                             dereg_mr_func_t dereg_mr_func)
    : numa_node(numa_node),
      reg_mr_func(reg_mr_func),
      dereg_mr_func(dereg_mr_func),
      stat_memory_reserved(0),
      stat_memory_allocated(0) {
  assert(numa_node <= kMaxNumaNodes);

  if (initial_size < kMinInitialSize) {
    initial_size = kMinInitialSize;
  }
  prev_allocation_size = initial_size;

  /*
   * Reserve initial hugepages. \p reserve_hugepages will throw a runtime
   * exception if reservation fails.
   */
  reserve_hugepages(initial_size, numa_node);
}

HugeAllocator::~HugeAllocator() {
  /* Deregister and delete the created SHM regions */
  for (shm_region_t &shm_region : shm_list) {
    dereg_mr_func(shm_region.mem_reg_info);
    delete_shm(shm_region.shm_key, shm_region.buf);
  }
}

/*
 * To create a cache of 4 KB pages, we first allocate the required number of
 * pages and then free them. This puts them all into the 4 KB freelist.
 */
bool HugeAllocator::create_4k_chunk_cache(size_t cache_chunks) {
  size_t reqd_chunks = cache_chunks - freelist[0].size();
  if (reqd_chunks <= 0) {
    return true;
  }

  std::vector<chunk_t> free_chunk_vec;

  for (size_t i = 0; i < reqd_chunks; i++) {
    uint32_t lkey;
    void *buf = alloc(KB(4), &lkey);
    if (buf == nullptr) {
      return false;
    }

    free_chunk_vec.push_back(chunk_t(buf, lkey));
  }

  for (size_t i = 0; i < reqd_chunks; i++) {
    free(free_chunk_vec[i], KB(4));
  }

  return true;
}

void HugeAllocator::print_stats() {
  fprintf(stderr, "eRPC HugeAllocator stats:\n");
  fprintf(stderr, "Total reserved memory = %zu bytes (%.2f MB)\n",
          stat_memory_reserved, (double)stat_memory_reserved / MB(1));
  fprintf(stderr, "Total memory allocated to users = %zu bytes (%.2f MB)\n",
          stat_memory_allocated, (double)stat_memory_allocated / MB(1));

  fprintf(stderr, "%zu SHM regions\n", shm_list.size());
  size_t shm_region_index = 0;
  for (shm_region_t &shm_region : shm_list) {
    fprintf(stderr, "Region %zu, size %zu MB\n", shm_region_index,
            shm_region.size / MB(1));
    shm_region_index++;
  }

  fprintf(stderr, "Size classes:\n");
  for (size_t i = 0; i < kNumClasses; i++) {
    size_t class_size = class_to_size(i);
    if (class_size < MB(1)) {
      fprintf(stderr, "\t%zu KB: %zu chunks\n", class_size / KB(1),
              freelist[i].size());
    } else {
      fprintf(stderr, "\t%zu MB: %zu chunks\n", class_size / MB(1),
              freelist[i].size());
    }
  }
}

bool HugeAllocator::reserve_hugepages(size_t size, size_t numa_node) {
  assert(size >= kMinInitialSize);

  std::ostringstream xmsg; /* The exception message */
  size = round_up<kHugepageSize>(size);
  int shm_key, shm_id;

  while (true) {
    /*
     * Choose a positive SHM key. Negative is fine but it looks scary in the
     * error message.
     */
    shm_key = static_cast<int>(slow_rand.next_u64());
    shm_key = std::abs(shm_key);

    /* Try to get an SHM region */
    shm_id = shmget(shm_key, size, IPC_CREAT | IPC_EXCL | 0666 | SHM_HUGETLB);

    if (shm_id == -1) {
      switch (errno) {
        case EEXIST:
          /* \p shm_key already exists. Try again. */
          break;

        case EACCES:
          xmsg << "eRPC HugeAllocator: SHM allocation error. "
               << "Insufficient permissions.";
          throw std::runtime_error(xmsg.str());

        case EINVAL:
          xmsg << "eRPC HugeAllocator: SHM allocation error: SHMMAX/SHMIN "
               << "mismatch. size = " << std::to_string(size) << " ("
               << std::to_string(size / MB(1)) << " MB)";
          throw std::runtime_error(xmsg.str());

        case ENOMEM:
          erpc_dprintf(
              "eRPC HugeAllocator: Insufficient memory. Can't reserve %lu MB\n",
              size / MB(1));
          return false;

        default:
          xmsg << "eRPC HugeAllocator: Unexpected SHM malloc error "
               << strerror(errno);
          throw std::runtime_error(xmsg.str());
      }
    } else {
      /* \p shm_key worked. Break out of the while loop */
      break;
    }
  }

  void *shm_buf = shmat(shm_id, nullptr, 0);
  if (shm_buf == nullptr) {
    xmsg << "eRPC HugeAllocator: SHM malloc error: shmat() failed for key "
         << std::to_string(shm_key);
    throw std::runtime_error(xmsg.str());
  }

  /* Bind the buffer to the NUMA node */
  const unsigned long nodemask = (1ul << (unsigned long)numa_node);
  long ret = mbind(shm_buf, size, MPOL_BIND, &nodemask, 32, 0);
  if (ret != 0) {
    xmsg << "eRPC HugeAllocator: SHM malloc error. mbind() failed for key "
         << shm_key;
    throw std::runtime_error(xmsg.str());
  }

  /*
   * If we are here, the allocation succeeded. Register the allocated buffer and
   * record in the \p shm_list.
   */
  memset(shm_buf, 0, size);
  MemRegInfo reg_info = reg_mr_func(shm_buf, size);

  shm_list.push_back(shm_region_t(shm_key, shm_buf, size, reg_info));
  stat_memory_reserved += size;

  /* Add chunks to the largest class */
  size_t num_chunks = size / kMaxAllocSize;
  assert(num_chunks >= 1);
  for (size_t i = 0; i < num_chunks; i++) {
    void *buf = (void *)((char *)shm_buf + (i * kMaxAllocSize));
    uint32_t lkey = reg_info.lkey;

    freelist[kNumClasses - 1].push_back(chunk_t(buf, lkey));
  }

  return true;
}

void HugeAllocator::delete_shm(int shm_key, const void *shm_buf) {
  int shmid = shmget(shm_key, 0, 0);
  if (shmid == -1) {
    switch (errno) {
      case EACCES:
        fprintf(stderr,
                "eRPC HugeAllocator: SHM free error: "
                "Insufficient permissions. SHM key = %d.\n",
                shm_key);
        break;
      case ENOENT:
        fprintf(stderr,
                "eRPC HugeAllocator: SHM free error: No such SHM key."
                "SHM key = %d.\n",
                shm_key);
        break;
      default:
        fprintf(stderr,
                "eRPC HugeAllocator: SHM free error: A wild SHM error: "
                "%s\n",
                strerror(errno));
        break;
    }

    exit(-1);
  }

  int ret = shmctl(shmid, IPC_RMID, nullptr); /* Please don't fail */
  if (ret != 0) {
    fprintf(stderr, "eRPC HugeAllocator: Error freeing SHM ID %d\n", shmid);
    exit(-1);
  }

  ret = shmdt(shm_buf);
  if (ret != 0) {
    fprintf(stderr,
            "HugeAllocator: Error freeing SHM buf %p. "
            "(SHM key = %d)\n",
            shm_buf, shm_key);
    exit(-1);
  }
}
}  // End ERpc

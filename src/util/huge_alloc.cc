#include "huge_alloc.h"

namespace ERpc {

HugeAllocator::HugeAllocator(size_t initial_size, size_t numa_node)
    : numa_node(numa_node),
      tot_free_hugepages(0),
      prev_allocation_size(initial_size),
      tot_memory_reserved(0),
      tot_memory_allocated(0) {
  assert(initial_size > 0 && initial_size <= kMaxAllocSize);
  assert(numa_node <= kMaxNumaNodes);

  /*
   * Reserve initial hugepages. \p reserve_hugepages will throw a runtime
   * exception if reservation fails.
   */
  reserve_hugepages(initial_size, numa_node);
}

HugeAllocator::~HugeAllocator() {
  /* Delete the created SHM regions */
  for (shm_region_t &shm_region : shm_list) {
    delete_shm(shm_region.key, shm_region.alloc_buf);
  }
}

void *HugeAllocator::alloc_huge(size_t size) {
  assert(size >= ERpc::kHugepageSize && size <= kMaxAllocSize);

  size = round_up<kHugepageSize>(size);
  size_t reqd_hugepages = size / kHugepageSize;

  /*
   * Try to get contiguous hugepages from an existing SHM region, starting
   * from the smallest (wrt allocation size).
   */
  for (shm_region_t &shm_region : shm_list) {
    void *huge_buf = alloc_contiguous(shm_region, reqd_hugepages);
    if (huge_buf != nullptr) {
      tot_memory_allocated += size;
      return huge_buf;
    }
  }

  /*
   * If we are here, no existing SHM region has sufficient hugepages. Increase
   * the allocation size, and ensure that we can allocate at least \p size.
   */
  prev_allocation_size *= 2;
  while (prev_allocation_size < size) {
    prev_allocation_size *= 2;
  }

  bool success = reserve_hugepages(prev_allocation_size, numa_node);
  if (!success) {
    /* We're out of hugepages */
    return nullptr;
  }

  /*
   * Use the last SHM region in the list to allocate. Other regions don't
   * have enough space.
   */
  shm_region_t &shm_region = shm_list.back();

  /*
   * This allocation must succeed - we allocated enough memory, and the SHM
   * region is not fragmented yet.
   */
  void *hugebuf_addr = alloc_contiguous(shm_region, reqd_hugepages);
  assert(hugebuf_addr != nullptr);

  tot_memory_allocated += size;
  return hugebuf_addr;
}

void HugeAllocator::free_huge(void *huge_buf) {
  if ((size_t)huge_buf % kHugepageSize != 0) {
    std::ostringstream xmsg;
    xmsg << "eRPC HugeAllocator: free_huge("
         << std::to_string((uintptr_t)huge_buf) << ") failed. ";
    throw std::runtime_error(xmsg.str());
  }

  /* Find the SHM region that was used to allocate \p huge_buf */
  for (shm_region_t &shm_region : shm_list) {
    void *hi = (void *)((char *)shm_region.alloc_buf + shm_region.alloc_size);

    if (huge_buf >= shm_region.alloc_buf && huge_buf < hi) {
      /* This is the SHM region. Find the number of SHM regions to free. */
      size_t hugepage_index =
          ((size_t)huge_buf - (size_t)shm_region.alloc_buf) / kHugepageSize;
      size_t nb_contig = shm_region.nb_contig_vec[hugepage_index];
      assert(nb_contig > 0);

      /* Mark the freed as free */
      for (size_t i = 0; i < nb_contig; i++) {
        assert(shm_region.free_hugepage_vec[hugepage_index + i] == false);
        shm_region.free_hugepage_vec[hugepage_index + i] = true;
      }

      /* Record the freeing */
      shm_region.nb_contig_vec[hugepage_index] = 0;
      shm_region.free_hugepages += nb_contig;

      tot_memory_allocated -= (nb_contig * kHugepageSize);
      return;
    }
  }

  /* We should never get here */
  std::ostringstream xmsg;
  xmsg << "eRPC HugeAllocator: free_huge("
       << std::to_string((uintptr_t)huge_buf) << ") failed. ";
  throw std::runtime_error(xmsg.str());
}

void HugeAllocator::print_stats() {
  fprintf(stderr, "eRPC HugeAllocator stats:\n");
  fprintf(stderr, "Total reserved memory = %zu bytes (%.2f MB)\n",
          tot_memory_reserved, (double)tot_memory_reserved / MB(1));
  fprintf(stderr, "Total memory allocated to users = %zu bytes (%.2f MB)\n",
          tot_memory_allocated, (double)tot_memory_allocated / MB(1));

  fprintf(stderr, "%zu SHM regions\n", shm_list.size());
  size_t shm_region_index = 0;
  for (shm_region_t &shm_region : shm_list) {
    fprintf(stderr, "Region %zu:\n", shm_region_index);
    shm_region_index++;

    fprintf(stderr, "\t%zu free hugepages\n", shm_region.free_hugepages);
    std::string bitvector_str;
    for (size_t i = 0; i < shm_region.alloc_hugepages; i++) {
      if (shm_region.free_hugepage_vec[i]) {
        bitvector_str += "1";
      } else {
        bitvector_str += "0";
      }
    }
    fprintf(stderr, "\tBitvector (1 means free page): %s\n",
            bitvector_str.c_str());
  }
}

void *HugeAllocator::alloc_contiguous(shm_region_t &shm_region,
                                      size_t num_hugepages) {
  if (shm_region.free_hugepages < num_hugepages) {
    return nullptr;
  }

  // Try to find a contiguous chunk of \p num_hugepages hugepages
  size_t start = 0; /* The start index of the contiguous chunk */

  while (start <= shm_region.alloc_hugepages - num_hugepages) {
    bool start_valid = true;
    size_t end = start;
    /* Check if pages [start, ..., start + num_hugepages) are all free */
    for (; end < start + num_hugepages; end++) {
      if (shm_region.free_hugepage_vec.at(end) == false) {
        start_valid = false;
        break;
      }
    }

    if (start_valid) {
      assert(end == start + num_hugepages - 1);
      assert(shm_region.free_hugepage_vec.at(end) == true);
      break;
    } else {
      start = end + 1;
    }
  }

  if (start == shm_region.alloc_hugepages) {
    /* We failed to find a valid chunk */
    return nullptr;
  } else {
    /* We have a valid chunk. Mark the hugepages in this chunk not-free */
    for (size_t i = 0; i < num_hugepages; i++) {
      shm_region.free_hugepage_vec.at(start + i) = false;
    }

    /* Record the allocation */
    shm_region.nb_contig_vec.at(start) = num_hugepages;
    shm_region.free_hugepages -= num_hugepages;

    tot_free_hugepages -= num_hugepages;

    void *ret_buf =
        (void *)((char *)shm_region.alloc_buf + start * kHugepageSize);
    return ret_buf;
  }
}

bool HugeAllocator::reserve_hugepages(size_t size, size_t numa_node) {
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
          xmsg << "eRPC HugeAllocator: SHM malloc error: SHMMAX/SHMIN "
               << "mismatch. size = " << std::to_string(size) << " ("
               << std::to_string(size / MB(1)) << " MB)";
          throw std::runtime_error(xmsg.str());

        case ENOMEM:
          erpc_dprintf(
              "eRPC HugeAllocator: SHM malloc error: Insufficient "
              "memory. SHM key = %d, size = %lu (%lu MB).\n",
              shm_key, size, size / MB(1));
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

  /* If we are here, the allocation succeeded. Record for deallocation. */
  memset(shm_buf, 0, size);

  shm_list.push_back(shm_region_t(shm_key, shm_buf, size));
  tot_free_hugepages += (size / kHugepageSize);
  tot_memory_reserved += size;

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

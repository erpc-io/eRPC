/**
 * @file misc.h
 * @brief Misc utility functions
 */

#ifndef ERPC_MISC_H
#define ERPC_MISC_H

#include <stdio.h>
#include <thread>
#include "common.h"

namespace erpc {

/// Bind thread to core \p numa_local_index on this CPU (specified by \p
/// numa_node). If the CPU has 2C Hyper Threads, calling with
/// n = {0, ..., C - 1} causes threads to be bound to Hyper Threads on distinct
/// cores.
/// XXX: This assumes that even cores are on NUMA 0 and odd cores on NUMA 1.
static void bind_to_core(std::thread &thread, size_t numa_node,
                         size_t numa_local_index) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  rt_assert(numa_node <= 1, "Invalid NUMA node");

  size_t global_index =
      numa_node == 0 ? numa_local_index * 2 : 1 + (numa_local_index * 2);
  CPU_SET(global_index, &cpuset);
  int rc = pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t),
                                  &cpuset);
  rt_assert(rc == 0, "Error setting thread affinity");
}

}  // End erpc

#endif

#pragma once

#include <numa.h>
#include <stdio.h>
#include <unistd.h>
#include <vector>
#include "common.h"

namespace erpc {

/// Return the number of logical cores per NUMA node
static size_t num_lcores_per_numa_node() {
  return static_cast<size_t>(numa_num_configured_cpus() /
                             numa_num_configured_nodes());
}

/// Return a list of logical cores in \p numa_node
static std::vector<size_t> get_lcores_for_numa_node(size_t numa_node) {
  rt_assert(numa_node <= static_cast<size_t>(numa_max_node()));

  std::vector<size_t> ret;
  size_t num_lcores = static_cast<size_t>(numa_num_configured_cpus());

  for (size_t i = 0; i < num_lcores; i++) {
    if (numa_node == static_cast<size_t>(numa_node_of_cpu(i))) {
      ret.push_back(i);
    }
  }

  return ret;
}

/// Bind \p thread to core with index \p numa_local_index on \p numa_node
static void bind_to_core(std::thread &thread, size_t numa_node,
                         size_t numa_local_index) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  rt_assert(numa_node <= kMaxNumaNodes, "Invalid NUMA node");

  auto lcore_vec = get_lcores_for_numa_node(numa_node);
  size_t global_index = lcore_vec.at(numa_local_index);

  CPU_SET(global_index, &cpuset);
  int rc = pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t),
                                  &cpuset);
  rt_assert(rc == 0, "Error setting thread affinity");
}

/// Reset this process's core mask to be all cores
static void clear_affinity_for_process() {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  const size_t num_cpus = static_cast<size_t>(sysconf(_SC_NPROCESSORS_ONLN));
  for (size_t i = 0; i < num_cpus; i++) CPU_SET(i, &mask);

  int ret = sched_setaffinity(0 /* whole-process */, sizeof(cpu_set_t), &mask);
  rt_assert(ret == 0, "Failed to clear CPU affinity for this process");
}

}  // namespace erpc

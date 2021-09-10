#include <stdio.h>
#include <thread>
#include <vector>
#include "common.h"
#include "util/logger.h"

#ifdef __linux__
#include <numa.h>
#else
#include <windows.h>
#endif

namespace erpc {

#ifdef __linux__
size_t num_lcores_per_numa_node() {
  return static_cast<size_t>(numa_num_configured_cpus() /
                             numa_num_configured_nodes());
}

std::vector<size_t> get_lcores_for_numa_node(size_t numa_node) {
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

void bind_to_core(std::thread &thread, size_t numa_node,
                  size_t numa_local_index) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  rt_assert(numa_node <= kMaxNumaNodes, "Invalid NUMA node");

  const std::vector<size_t> lcore_vec = get_lcores_for_numa_node(numa_node);
  if (numa_local_index >= lcore_vec.size()) {
    ERPC_ERROR(
        "eRPC: Requested binding to core %zu (zero-indexed) on NUMA node %zu, "
        "which has only %zu cores. Ignoring, but this can cause very low "
        "performance.\n",
        numa_local_index, numa_node, lcore_vec.size());
    return;
  }

  const size_t global_index = lcore_vec.at(numa_local_index);

  CPU_SET(global_index, &cpuset);
  int rc = pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t),
                                  &cpuset);
  rt_assert(rc == 0, "Error setting thread affinity");
}

void clear_affinity_for_process() {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  const size_t num_cpus = std::thread::hardware_concurrency();
  for (size_t i = 0; i < num_cpus; i++) CPU_SET(i, &mask);

  int ret = sched_setaffinity(0 /* whole-process */, sizeof(cpu_set_t), &mask);
  rt_assert(ret == 0, "Failed to clear CPU affinity for this process");
}

#else  // Windows

size_t num_lcores_per_numa_node() {
  return std::thread::hardware_concurrency();
}

std::vector<size_t> get_lcores_for_numa_node(size_t numa_node) {
  rt_assert(numa_node == 0,
            "eRPC/Windows currently supports only one NUMA node");
  std::vector<size_t> ret;
  for (size_t i = 0; i < num_lcores_per_numa_node(); i++) ret.push_back(i);
  return ret;
}

void bind_to_core(std::thread &thread, size_t numa_node,
                  size_t numa_local_index) {
  rt_assert(numa_node == 0,
            "eRPC/Windows currently supports only one NUMA node");
  rt_assert(numa_local_index < num_lcores_per_numa_node() and
                numa_local_index < 8 * sizeof(DWORD_PTR),
            "Requested core index is too high");

  ERPC_INFO("eRPC: Binding thread to core %zu\n", numa_local_index);

  const DWORD_PTR dw = SetThreadAffinityMask(thread.native_handle(),
                                             DWORD_PTR(1) << numa_local_index);
  if (dw == 0) {
    ERPC_ERROR(
        "eRPC: SetThreadAffinityMask failed with error %lu. Ignoring, but this "
        "can cause very low performance.\n",
        GetLastError());
  }
}

void clear_affinity_for_process() { return; }
#endif
}  // namespace erpc

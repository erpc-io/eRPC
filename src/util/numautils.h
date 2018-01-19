#ifndef ERPC_MISC_H
#define ERPC_MISC_H

#include <stdio.h>
#include <boost/algorithm/string.hpp>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include "common.h"

namespace erpc {

static std::vector<size_t> get_lcores_for_numa_node(size_t numa_node) {
  std::ifstream infile("/proc/cpuinfo");
  std::vector<size_t> numa_lcores[4];

  std::string line;
  std::vector<std::string> split_vec;

  size_t cur_lcore = 0;
  while (std::getline(infile, line)) {
    if (line.find("processor") != std::string::npos) {
      boost::split(split_vec, line, boost::is_any_of(":"));
      boost::trim(split_vec.at(1));

      int file_lcore = std::atoi(split_vec.at(1).c_str());
      assert(file_lcore == static_cast<int>(cur_lcore));
    }

    if (line.find("physical id") != std::string::npos) {
      boost::split(split_vec, line, boost::is_any_of(":"));
      boost::trim(split_vec.at(1));

      int numa = std::atoi(split_vec.at(1).c_str());
      assert(numa >= 0 && numa <= static_cast<int>(kMaxNumaNodes));

      numa_lcores[numa].push_back(cur_lcore);
      cur_lcore++;
    }
  }

  return numa_lcores[numa_node];
}

/// Bind thread to core \p numa_local_index on this NUMA node
static void bind_to_core(std::thread &thread, size_t numa_node,
                         size_t numa_local_index) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  rt_assert(numa_node <= kMaxNumaNodes, "Invalid NUMA node");

  auto lcore_vec = get_lcores_for_numa_node(numa_node);
  rt_assert(numa_local_index < lcore_vec.size(),
            "Insufficient lcores in NUMA node");

  size_t global_index = lcore_vec.at(numa_local_index);
  CPU_SET(global_index, &cpuset);
  int rc = pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t),
                                  &cpuset);
  rt_assert(rc == 0, "Error setting thread affinity");
}

}  // End erpc

#endif

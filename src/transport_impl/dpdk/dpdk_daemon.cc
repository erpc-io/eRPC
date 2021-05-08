/**
 * @file dpdk_daemon.cc
 * @brief A DPDK management daemon that initializes DPDK queues and mempools
 * for processes that use eRPC.
 */

#include <gflags/gflags.h>
#include "dpdk_externs.h"
#include "dpdk_transport.h"

DEFINE_uint64(phy_port, 0, "DPDK port ID to manage with this daemon");
DEFINE_uint64(numa_node, 0,
              "NUMA node for the DPDK port managed by this daemon");

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  erpc::ERPC_WARN("eRPC DPDK daemon: Managing DPDK port %zu on NUMA node %zu\n",
                  FLAGS_phy_port, FLAGS_numa_node);

  // n: channels, m: maximum memory in megabytes
  const char *rte_argv[] = {
      "--proc-type", "primary",                                            //
      "-c",          "1",                                                  //
      "-n",          "6",                                                  //
      "--log-level", (ERPC_LOG_LEVEL >= ERPC_LOG_LEVEL_INFO) ? "8" : "0",  //
      "-m",          "1024",                                               //
      nullptr};

  int rte_argc = static_cast<int>(sizeof(rte_argv) / sizeof(rte_argv[0])) - 1;
  int ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
  erpc::rt_assert(ret >= 0,
                  "eRPC DPDK daemon: Failed to initialize DPDK. Is another "
                  "eRPC DPDK daemon or primary DPDK process already running?");

  erpc::DpdkTransport::setup_phy_port(
      FLAGS_phy_port, FLAGS_numa_node,
      erpc::DpdkTransport::DpdkProcType::kPrimary);

  sleep(100000);
  return 0;
}

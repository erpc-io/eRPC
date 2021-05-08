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
  erpc::rt_assert(!getuid(), "You need to be root to use eRPC");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  erpc::ERPC_WARN("eRPC DPDK daemon: Managing DPDK port %zu on NUMA node %zu\n",
                  FLAGS_phy_port, FLAGS_numa_node);

  // clang-format off
  const char *rte_argv[] = {
      "-c",          "1",
      "-n",          "6",  // Memory channels
      "-m",          "1024", // Max memory in megabytes
      "--proc-type", "primary",
      "--log-level", (ERPC_LOG_LEVEL >= ERPC_LOG_LEVEL_INFO) ? "8" : "0",
      nullptr};
  // clang-format on

  int rte_argc = static_cast<int>(sizeof(rte_argv) / sizeof(rte_argv[0])) - 1;
  int ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
  if (ret < 0) {
    fprintf(stderr,
            "eRPC DPDK daemon: Failed to initialize DPDK. Is another "
            "eRPC DPDK daemon or primary DPDK process already running? DPDK "
            "error code = %s.\n",
            rte_strerror(rte_errno));
    exit(-1);
  }

  erpc::rt_assert(
      ret >= 0,
      "eRPC DPDK daemon: Error: Failed to initialize DPDK. Is another "
      "eRPC DPDK daemon or primary DPDK process already running?");

  erpc::ERPC_WARN("eRPC DPDK daemon: Successfully initialized DPDK EAL\n");

  erpc::DpdkTransport::setup_phy_port(
      FLAGS_phy_port, FLAGS_numa_node,
      erpc::DpdkTransport::DpdkProcType::kPrimary);

  erpc::ERPC_WARN("eRPC DPDK daemon: Successfully initialized DPDK port %zu\n",
                  FLAGS_phy_port);

  // Check if the link is up
  struct rte_eth_link link;
  rte_eth_link_get(static_cast<uint8_t>(FLAGS_phy_port), &link);
  if (link.link_status != ETH_LINK_UP) {
    fprintf(stderr, "eRPC DPDK daemon: Error: Port %zu link is down\n",
            FLAGS_phy_port);
    exit(-1);
  }

  sleep(100000);
  return 0;
}

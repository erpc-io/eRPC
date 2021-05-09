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

  const std::string memzone_name = erpc::DpdkTransport::get_memzone_name();
  const rte_memzone *memzone = rte_memzone_reserve(
      memzone_name.c_str(), sizeof(erpc::DpdkTransport::ownership_memzone_t),
      FLAGS_numa_node, RTE_MEMZONE_2MB);
  erpc::rt_assert(memzone != nullptr,
                  "eRPC DPDK daemon: Failed to create memzone");
  erpc::ERPC_WARN(
      "eRPC DPDK daemon: Successfully initialized shared memzone %s\n",
      memzone_name.c_str());

  auto *memzone_contents =
      reinterpret_cast<erpc::DpdkTransport::ownership_memzone_t *>(
          memzone->addr);
  memzone_contents->init();

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
  memzone_contents->link_[FLAGS_phy_port] = link;

  size_t prev_epoch = 0;
  while (true) {
    sleep(1);
    size_t cur_epoch = memzone_contents->get_epoch();
    if (cur_epoch != prev_epoch) {
      erpc::ERPC_WARN("eRPC DPDK daemon: %s\n",
                      memzone_contents->get_summary(FLAGS_phy_port).c_str());
      prev_epoch = cur_epoch;
    }
  }
  return 0;
}

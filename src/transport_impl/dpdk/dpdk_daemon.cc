/**
 * @file dpdk_daemon.cc
 * @brief A DPDK management daemon that initializes DPDK queues and mempools
 * for processes that use eRPC.
 */

#include <gflags/gflags.h>
#include <chrono>
#include <vector>
#include "dpdk_externs.h"
#include "dpdk_transport.h"

DEFINE_uint64(phy_port, 0, "DPDK port ID to manage with this daemon");
DEFINE_uint64(numa_node, 0,
              "NUMA node for the DPDK port managed by this daemon");
DEFINE_bool(loopback, true,
            "Enable NIC hardware loopback mode");
DEFINE_int64(peer_phy_port, -1,
             "Optional second DPDK port to initialize");

int main(int argc, char **argv) {
  erpc::rt_assert(!getuid(), "You need to be root to use eRPC");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  ERPC_WARN("eRPC DPDK daemon: Managing DPDK port %zu on NUMA node %zu\n",
            FLAGS_phy_port, FLAGS_numa_node);

  erpc::rt_assert(FLAGS_phy_port < erpc::kMaxPhyPorts,
                  "Invalid --phy_port");
  erpc::rt_assert(FLAGS_peer_phy_port >= -1 &&
                      FLAGS_peer_phy_port < static_cast<int64_t>(erpc::kMaxPhyPorts),
                  "Invalid --peer_phy_port");
  std::vector<uint16_t> phy_ports{static_cast<uint16_t>(FLAGS_phy_port)};
  if (FLAGS_peer_phy_port >= 0) {
    erpc::rt_assert(static_cast<uint64_t>(FLAGS_peer_phy_port) != FLAGS_phy_port,
                    "--peer_phy_port must differ from --phy_port");
    phy_ports.push_back(static_cast<uint16_t>(FLAGS_peer_phy_port));
  }

  // clang-format off
  const char *rte_argv[] = {
      "erpc_dpdk_daemon",
      "-c",          "1",
      "-n",          "6",  // Memory channels
      "-m",          "1024", // Max memory in megabytes
      "--proc-type", "primary",
      "--log-level", (ERPC_LOG_LEVEL >= ERPC_LOG_LEVEL_INFO) ? "8" : "4",
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

  ERPC_WARN("eRPC DPDK daemon: Successfully initialized DPDK EAL\n");

  for (uint16_t phy_port : phy_ports) {
    erpc::rt_assert(rte_eth_dev_is_valid_port(phy_port),
                    "Invalid DPDK port " + std::to_string(phy_port));
  }

  const std::string memzone_name = erpc::DpdkTransport::get_memzone_name();
  const rte_memzone *memzone = rte_memzone_reserve(
      memzone_name.c_str(), sizeof(erpc::DpdkTransport::ownership_memzone_t),
      FLAGS_numa_node, RTE_MEMZONE_2MB);
  erpc::rt_assert(memzone != nullptr,
                  "eRPC DPDK daemon: Failed to create memzone");
  ERPC_WARN("eRPC DPDK daemon: Successfully initialized shared memzone %s\n",
            memzone_name.c_str());

  auto *ownership_memzone =
      reinterpret_cast<erpc::DpdkTransport::ownership_memzone_t *>(
          memzone->addr);
  ownership_memzone->init();

  // Start both ends of a local DAC connection before waiting for either link.
  for (uint16_t phy_port : phy_ports) {
    erpc::DpdkTransport::setup_phy_port(
        phy_port, FLAGS_numa_node,
        erpc::DpdkTransport::DpdkProcType::kPrimary, FLAGS_loopback);
    ownership_memzone->reta_size_[phy_port] =
        erpc::DpdkTransport::get_port_reta_size(phy_port);
    ERPC_WARN("eRPC DPDK daemon: Successfully initialized DPDK port %u\n",
              phy_port);
  }

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(10);
  while (true) {
    bool all_up = true;
    for (uint16_t phy_port : phy_ports) {
      struct rte_eth_link link = {};
      ret = rte_eth_link_get_nowait(phy_port, &link);
      erpc::rt_assert(ret == 0, "Failed to query link for DPDK port " +
                                   std::to_string(phy_port));
      ownership_memzone->link_[phy_port] = link;
      if (link.link_status != ETH_LINK_UP) all_up = false;
    }
    if (all_up) break;
    if (std::chrono::steady_clock::now() >= deadline) {
      for (uint16_t phy_port : phy_ports) {
        if (ownership_memzone->link_[phy_port].link_status != ETH_LINK_UP) {
          fprintf(stderr, "eRPC DPDK daemon: Port %u link is down after 10 seconds\n",
                  phy_port);
        }
      }
      return EXIT_FAILURE;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  ERPC_WARN("eRPC DPDK daemon: All managed links are up; ready for applications\n");

  size_t prev_epoch = 0;
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    size_t cur_epoch = ownership_memzone->get_epoch();
    if (cur_epoch != prev_epoch) {
      for (uint16_t phy_port : phy_ports) {
        ERPC_WARN("eRPC DPDK daemon: Port %u: %s\n", phy_port,
                  ownership_memzone->get_summary(phy_port).c_str());
      }
      prev_epoch = cur_epoch;
    }
    for (uint16_t phy_port : phy_ports) {
      ownership_memzone->daemon_reclaim_qps_from_crashed(phy_port);
    }
  }
  return 0;
}

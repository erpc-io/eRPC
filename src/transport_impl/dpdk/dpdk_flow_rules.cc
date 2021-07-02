/**
 * @file dpdk_flow_rules.cc
 * @brief Helpers for installing Flow Director rules in DPDK
 */
#pragma once

#ifdef ERPC_DPDK

#include <rte_ethdev.h>
#include "common.h"
#include "util/logger.h"

namespace erpc {

#ifdef _WIN32

static void install_flow_rule(size_t phy_port, size_t qp_id, uint32_t ipv4_addr,
                              uint16_t udp_port) {
  rt_assert(false, "Not implemented for Windows\n");
}

#else   // _WIN32

static void install_flow_rule(size_t phy_port, size_t qp_id, uint32_t ipv4_addr,
                              uint16_t udp_port) {
  bool installed = false;

  const int ntuple_filter_supported =
      rte_eth_dev_filter_supported(phy_port, RTE_ETH_FILTER_NTUPLE);

  const int fdir_filter_supported =
      rte_eth_dev_filter_supported(phy_port, RTE_ETH_FILTER_FDIR);

  if (ntuple_filter_supported != 0 && fdir_filter_supported != 0) {
    ERPC_WARN("No flow steering supported by NIC. Apps likely won't work.\n");
    return;
  }

  // Try the simplest filter first. I couldn't get FILTER_FDIR to work with
  // ixgbe, although it technically supports flow director.
  if (ntuple_filter_supported == 0) {
    struct rte_eth_ntuple_filter ntuple;
    memset(&ntuple, 0, sizeof(ntuple));
    ntuple.flags = RTE_5TUPLE_FLAGS;
    ntuple.dst_port = rte_cpu_to_be_16(udp_port);
    ntuple.dst_port_mask = UINT16_MAX;
    ntuple.dst_ip = rte_cpu_to_be_32(ipv4_addr);
    ntuple.dst_ip_mask = UINT32_MAX;
    ntuple.proto = IPPROTO_UDP;
    ntuple.proto_mask = UINT8_MAX;
    ntuple.priority = 1;
    ntuple.queue = qp_id;

    int ret = rte_eth_dev_filter_ctrl(phy_port, RTE_ETH_FILTER_NTUPLE,
                                      RTE_ETH_FILTER_ADD, &ntuple);
    if (ret != 0) {
      ERPC_WARN("Failed to add ntuple filter. This could be survivable.\n");
    } else {
      ERPC_WARN("Installed ntuple flow rule. Queue %zu, RX UDP port = %u.\n",
                qp_id, udp_port);
    }
    installed = (ret == 0);
  }

  if (!installed && fdir_filter_supported == 0) {
    // Use fdir filter for i40e (5-tuple not supported)
    rte_eth_fdir_filter filter;
    memset(&filter, 0, sizeof(filter));
    filter.soft_id = qp_id;
    filter.input.flow_type = RTE_ETH_FLOW_NONFRAG_IPV4_UDP;
    filter.input.flow.udp4_flow.dst_port = rte_cpu_to_be_16(udp_port);
    filter.input.flow.udp4_flow.ip.dst_ip = rte_cpu_to_be_32(ipv4_addr);
    filter.action.rx_queue = qp_id;
    filter.action.behavior = RTE_ETH_FDIR_ACCEPT;
    filter.action.report_status = RTE_ETH_FDIR_NO_REPORT_STATUS;

    int ret = rte_eth_dev_filter_ctrl(phy_port, RTE_ETH_FILTER_FDIR,
                                      RTE_ETH_FILTER_ADD, &filter);
    rt_assert(ret == 0, "Failed to add fdir flow rule: ", strerror(-1 * ret));

    ERPC_WARN("Installed flow-director rule. Queue %zu, RX UDP port = %u.\n",
              qp_id, udp_port);
  }
}
#endif  // _WIN32

}  // namespace erpc
#endif  // ERPC_DPDK

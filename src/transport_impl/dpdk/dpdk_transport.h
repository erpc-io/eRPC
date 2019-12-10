/**
 * @file dpdk_transport.h
 * @brief Transport for any DPDK-supported NIC
 */
#pragma once

#ifdef ERPC_DPDK

#include "transport.h"
#include "transport_impl/eth_common.h"
#include "util/barrier.h"
#include "util/logger.h"

#include <rte_common.h>
#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_mbuf.h>

namespace erpc {

class DpdkTransport : public Transport {
 public:
  // Transport-specific constants
  static constexpr TransportType kTransportType = TransportType::kDPDK;
  static constexpr size_t kMTU = 1024;
  static constexpr size_t kMaxQueuesPerPort = 16;

  static constexpr size_t kNumTxRingDesc = 128;
  static constexpr size_t kPostlist = 32;

  // The PMD may inline internally, but this class doesn't do it
  static constexpr size_t kMaxInline = 0;

  // For now, this is just for erpc::Rpc to size its array of control Msgbufs
  static constexpr size_t kUnsigBatch = 32;

  /// Maximum number of packets received in rx_burst
  static constexpr size_t kRxBatchSize = 32;

  /// Number of mbufs in each mempool (one per Transport instance). The DPDK
  /// docs recommend power-of-two minus one mbufs per pool for best utilization.
  static constexpr size_t kNumMbufs = (kNumRxRingEntries * 2 - 1);

  // XXX: ixgbe does not support fast free offload, but i40e does
  static constexpr uint32_t kOffloads = DEV_TX_OFFLOAD_MULTI_SEGS;

  /// Per-element size for the packet buffer memory pool
  static constexpr size_t kMbufSize =
      (static_cast<uint32_t>(sizeof(struct rte_mbuf)) + RTE_PKTMBUF_HEADROOM +
       kMTU);

  /// Maximum data bytes (i.e., non-header) in a packet
  static constexpr size_t kMaxDataPerPkt = (kMTU - sizeof(pkthdr_t));

  DpdkTransport(uint16_t sm_udp_port, uint8_t rpc_id, uint8_t phy_port,
                size_t numa_node, FILE *trace_file);
  void init_hugepage_structures(HugeAlloc *huge_alloc, uint8_t **rx_ring);

  ~DpdkTransport();

  void fill_local_routing_info(RoutingInfo *routing_info) const;
  bool resolve_remote_routing_info(RoutingInfo *routing_info) const;
  size_t get_bandwidth() const { return resolve.bandwidth; }

  static std::string routing_info_str(RoutingInfo *ri) {
    return reinterpret_cast<eth_routing_info_t *>(ri)->to_string();
  }

  static std::string dpdk_strerror() {
    return std::string(rte_strerror(rte_errno));
  }

  /// Convert an RX ring data pointer to its corresponding DPDK mbuf pointer
  static rte_mbuf *dpdk_dtom(uint8_t *data) {
    return reinterpret_cast<rte_mbuf *>(data - RTE_PKTMBUF_HEADROOM -
                                        sizeof(rte_mbuf));
  }

  /// Return the UDP port to use for queue \p qp_id on DPDK port \p phy_port.
  /// With DPDK, only one process is allowed to use \p phy_port, so we need not
  /// account for other processes.
  static uint16_t udp_port_for_queue(size_t phy_port, size_t qp_id) {
    return kBaseEthUDPPort + (phy_port * kMaxQueuesPerPort) + qp_id;
  }

  /// Get the IPv4 address for \p phy_port. The returned IPv4 address is assumed
  /// to be in host-byte order.
  static uint32_t get_port_ipv4_addr(size_t phy_port) {
    // For now, we use the LSBs of the port's MAC address
    struct rte_ether_addr mac;
    rte_eth_macaddr_get(phy_port, &mac);

    uint32_t ret;
    memcpy(&ret, &mac.addr_bytes[2], sizeof(ret));
    return ret;
  }

  // raw_transport_datapath.cc
  void tx_burst(const tx_burst_item_t *tx_burst_arr, size_t num_pkts);
  void tx_flush();
  size_t rx_burst();
  void post_recvs(size_t num_recvs);

 private:
  /// Do DPDK initialization for \p phy_port. \p phy_port must not have been
  /// initialized.
  void setup_phy_port();

  /**
   * @brief Resolve fields in \p resolve using \p phy_port
   * @throw runtime_error if the port cannot be resolved
   */
  void resolve_phy_port();

  /// Install a flow rule for queue \p qp_id on port \p phy_port. The IPv4 and
  /// UDP address are in host-byte order.
  static void install_flow_rule(size_t phy_port, size_t qp_id,
                                uint32_t ipv4_addr, uint16_t udp_port) {
    bool installed = false;

    // Try the simplest filter first. I couldn't get FILTER_FDIR to work with
    // ixgbe, although it technically supports flow director.
    if (rte_eth_dev_filter_supported(phy_port, RTE_ETH_FILTER_NTUPLE) == 0) {
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

    if (!installed &&
        rte_eth_dev_filter_supported(phy_port, RTE_ETH_FILTER_FDIR) == 0) {
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
      rt_assert(ret == 0, "Failed to add flow rule: ", strerror(-1 * ret));

      ERPC_WARN("Installed flow-director rule. Queue %zu, RX UDP port = %u.\n",
                qp_id, udp_port);
    }
  }

  /// Initialize the memory registration and deregistration functions
  void init_mem_reg_funcs();

  /// For DPDK, the RX ring buffers might not always be used in a circular
  /// order. Instead, we write pointers to the Rpc's RX ring.
  uint8_t **rx_ring;

  size_t rx_ring_head = 0, rx_ring_tail = 0;

  uint16_t rx_flow_udp_port = 0;
  size_t qp_id = SIZE_MAX;  ///< The RX/TX queue pair for this Transport

  // We don't use DPDK's lcore threads, so a shared mempool with per-lcore
  // cache won't work. Instead, we use per-thread pools with zero cached mbufs.
  rte_mempool *mempool;

  /// Info resolved from \p phy_port, must be filled by constructor.
  struct {
    uint32_t ipv4_addr;    ///< The port's IPv4 address in host-byte order
    uint8_t mac_addr[6];   ///< The port's MAC address
    size_t bandwidth = 0;  ///< Link bandwidth in bytes per second
  } resolve;
};

}  // namespace erpc

#endif

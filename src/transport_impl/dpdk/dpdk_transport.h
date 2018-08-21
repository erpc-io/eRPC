/**
 * @file dpdk_transport.h
 * @brief Transport for any DPDK-supported NIC
 */
#pragma once

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
  static constexpr size_t kMaxQueues = 16;

  static constexpr size_t kNumTxRingDesc = 128;
  static constexpr size_t kPostlist = 32;

  // The PMD may inline internally, but this class doesn't do it
  static constexpr size_t kMaxInline = 0;

  // For now, this is just for erpc::Rpc to size its array of control Msgbufs
  static constexpr size_t kUnsigBatch = 32;

  /// Maximum number of packets received in rx_burst
  static constexpr size_t kRxBatchSize = 32;

  static constexpr size_t kNumMbufs = (kNumRxRingEntries * 2 - 1);
  const char *kTempIp = "10.10.1.1";  // XXX: Temporary IP for everyone

  // XXX: ixgbe does not support fast free offload, but i40e does
  static constexpr uint32_t kOffloads = DEV_TX_OFFLOAD_MULTI_SEGS;

  /// Per-element size for the packet buffer memory pool
  static constexpr size_t kMbufSize =
      (static_cast<uint32_t>(sizeof(struct rte_mbuf)) + RTE_PKTMBUF_HEADROOM +
       kMTU);

  /// Maximum data bytes (i.e., non-header) in a packet
  static constexpr size_t kMaxDataPerPkt = (kMTU - sizeof(pkthdr_t));

  DpdkTransport(uint8_t rpc_id, uint8_t phy_port, size_t numa_node,
                FILE *trace_file);
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

  // raw_transport_datapath.cc
  void tx_burst(const tx_burst_item_t *tx_burst_arr, size_t num_pkts);
  void tx_flush();
  size_t rx_burst();
  void post_recvs(size_t num_recvs);

 private:
  /// Perform once-per-process DPDK initialization
  void do_per_process_dpdk_init();

  /**
   * @brief Resolve fields in \p resolve using \p phy_port
   * @throw runtime_error if the port cannot be resolved
   */
  void resolve_phy_port();
  void install_flow_rule();  ///< Install the UDP destination flow

  /// Initialize the memory registration and deregistration functions
  void init_mem_reg_funcs();

  /// For DPDK, the RX ring buffers might not always be used in a circular
  /// order. Instead, we write pointers to the Rpc's RX ring.
  uint8_t **rx_ring;

  size_t rx_ring_head = 0, rx_ring_tail = 0;

  const uint16_t rx_flow_udp_port;
  size_t qp_id;  ///< The DPDK RX/TX queue pair for this transport endpoint

  // We don't use DPDK's lcore threads, so a shared mempool with per-lcore
  // cache won't work. Instead, we use per-thread pools with zero cached mbufs.
  rte_mempool *mempool;

  /// Info resolved from \p phy_port, must be filled by constructor.
  struct {
    uint32_t ipv4_addr;    ///< The port's IPv4 address
    uint8_t mac_addr[6];   ///< The port's MAC address
    size_t bandwidth = 0;  ///< Link bandwidth in bytes per second
  } resolve;
};

}  // namespace erpc

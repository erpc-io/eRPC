/**
 * @file dpdk_transport.h
 * @brief Transport for any DPDK-supported NIC
 */
#pragma once

#include "transport.h"
#include "transport_impl/eth_common.h"
#include "util/barrier.h"
#include "util/logger.h"

namespace erpc {

class DpdkTransport : public Transport {
 public:
  // Transport-specific constants
  static constexpr TransportType kTransportType = TransportType::kDPDK;
  static constexpr size_t kMTU = 1024;

  /// Maximum data bytes (i.e., non-header) in a packet
  static constexpr size_t kMaxDataPerPkt = (kMTU - sizeof(pkthdr_t));

  DpdkTransport(uint8_t rpc_id, uint8_t phy_port, size_t numa_node,
               FILE *trace_file);
  void init_hugepage_structures(HugeAlloc *huge_alloc, uint8_t **rx_ring);


  ~DpdkTransport();

  void fill_local_routing_info(RoutingInfo *routing_info) const;
  bool resolve_remote_routing_info(RoutingInfo *routing_info) const;

  static std::string routing_info_str(RoutingInfo *ri) {
    return reinterpret_cast<eth_routing_info_t *>(ri)->to_string();
  }

  // raw_transport_datapath.cc
  void tx_burst(const tx_burst_item_t *tx_burst_arr, size_t num_pkts);
  void tx_flush();
  size_t rx_burst();
  void post_recvs(size_t num_recvs);

 private:
  /**
   * @brief Resolve fields in \p resolve using \p phy_port
   * @throw runtime_error if the port cannot be resolved
   */
  void resolve_phy_port();

  /// Initialize the memory registration and deregistration functions
  void init_mem_reg_funcs();

  /// Initialize structures that do not require eRPC hugepages: device
  /// context, protection domain, and queue pairs.
  void init_verbs_structs();
  void init_send_qp();  ///< Iniitalize the SEND QP

  /// In the dumbpipe mode, initialze the multi-packet RECV QP
  void init_mp_recv_qp();
  void install_flow_rule();               ///< Install the UDP destination flow

  /// Initialize constant fields of RECV descriptors, fill in the Rpc's
  /// RX ring, and fill the RECV queue.
  void init_recvs(uint8_t **rx_ring);
  void init_sends();  ///< Initialize constant fields of SEND work requests

  /// Info resolved from \p phy_port, must be filled by constructor.
  struct {
    uint32_t ipv4_addr;       ///< The port's IPv4 address
    uint8_t mac_addr[6];      ///< The port's MAC address
  } resolve;
};

}  // End erpc

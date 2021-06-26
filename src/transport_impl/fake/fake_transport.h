/**
 * @file fake_transport.h
 * @brief Implementation of a dummy transport to test compilation on machines
 * that lack verbs and DPDK.
 */
#pragma once

#ifdef ERPC_FAKE

#include "transport.h"

namespace erpc {

class FakeTransport : public Transport {
 public:
  static constexpr TransportType kTransportType = TransportType::kFake;
  static constexpr size_t kMTU = 1024;
  static constexpr size_t kPostlist = 16;
  static constexpr size_t kUnsigBatch = 64;
  static constexpr size_t kMaxDataPerPkt = (kMTU - sizeof(pkthdr_t));

  FakeTransport(uint16_t, uint8_t rpc_id, uint8_t phy_port, size_t numa_node,
                FILE *trace_file)
      : Transport(TransportType::kRaw, rpc_id, phy_port, numa_node,
                  trace_file) {}

  inline void init_hugepage_structures(HugeAlloc *, uint8_t **) {}
  inline void fill_local_routing_info(routing_info_t *) const {}

  inline bool resolve_remote_routing_info(routing_info_t *) { return false; }
  inline size_t get_bandwidth() const { return 0; }

  static std::string routing_info_str(routing_info_t *) { return ""; }

  void tx_burst(const tx_burst_item_t *, size_t) {}
  void tx_flush() {}
  size_t rx_burst() { return 0; }
  void post_recvs(size_t) {}
};

}  // namespace erpc

#endif

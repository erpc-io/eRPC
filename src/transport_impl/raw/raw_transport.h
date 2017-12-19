/**
 * @file raw_transport.h
 * @brief Transport for Mellanox verbs-based raw Ethernet
 */
#ifndef ERPC_RAW_TRANSPORT_H
#define ERPC_RAW_TRANSPORT_H

#include "inet_hdrs.h"
#include "transport.h"
#include "transport_impl/verbs_common.h"
#include "util/logger.h"

namespace erpc {

class RawTransport : public Transport {
 public:
  // Transport-specific constants
  static constexpr TransportType kTransportType = TransportType::kRaw;
  static constexpr size_t kMTU = 1500;
  static constexpr size_t kRecvSize = (kMTU + 64);  ///< RECV size (with GRH)
  static constexpr size_t kSQDepth = 128;           ///< Send queue depth
  static constexpr size_t kUnsigBatch = 64;  ///< Selective signaling for SENDs
  static constexpr size_t kPostlist = 16;    ///< Maximum SEND postlist
  static constexpr size_t kMaxInline = 60;   ///< Maximum send wr inline data
  static constexpr size_t kRecvSlack = 32;   ///< RECVs batched before posting

  static_assert(kSQDepth >= 2 * kUnsigBatch, "");     // Queue capacity check
  static_assert(kPostlist <= kUnsigBatch, "");        // Postlist check
  static_assert(kMaxInline >= sizeof(pkthdr_t), "");  // Inline control msgs

  // Derived constants

  /// Maximum data bytes (i.e., non-header) in a packet
  static constexpr size_t kMaxDataPerPkt = (kMTU - sizeof(pkthdr_t));

  /// Session endpoint routing info for raw Ethernet.
  struct raw_routing_info_t {
    uint8_t mac[6];
    uint32_t ip_addr;
    uint16_t udp_port;
  };
  static_assert(sizeof(raw_routing_info_t) <= kMaxRoutingInfoSize, "");

  RawTransport(uint8_t phy_port, uint8_t rpc_id);
  void init_hugepage_structures(HugeAlloc *huge_alloc, uint8_t **rx_ring);

  ~RawTransport();

  void fill_local_routing_info(RoutingInfo *routing_info) const;
  bool resolve_remote_routing_info(RoutingInfo *routing_info) const;

  static std::string routing_info_str(RoutingInfo *routing_info) {
    auto *ri = reinterpret_cast<raw_routing_info_t *>(routing_info);

    std::ostringstream ret;
    ret << "[MAC " << mac_to_string(ri->mac) << ", IP "
        << ip_to_string(ri->ip_addr) << ", UDP port "
        << std::to_string(ri->udp_port) << "]";

    return std::string(ret.str());
  }

  // ib_transport_datapath.cc
  void tx_burst(const tx_burst_item_t *tx_burst_arr, size_t num_pkts);
  void tx_flush();
  void poll_send_cq_for_flush(bool first);
  size_t rx_burst();
  void post_recvs(size_t num_recvs);

  /// Get the current SEND signaling flag, and poll the send CQ if we need to
  inline int get_signaled_flag() {
    // If kUnsigBatch is 4, the sequence of signaling and polling looks like so:
    // * nb_tx = 0: no poll, signaled
    // * nb_tx = 1: no poll, unsignaled
    // * nb_tx = 2: no poll, unsignaled
    // * nb_tx = 3: no poll, unsignaled
    // * nb_tx = 4: poll, signaled
    static_assert(is_power_of_two(kUnsigBatch), "");

    int flag;
    if ((nb_tx & (kUnsigBatch - 1)) == 0) {
      flag = IBV_SEND_SIGNALED;

      if (likely(nb_tx != 0)) {
        struct ibv_wc wc;
        while (ibv_poll_cq(send_cq, 1, &wc) == 0) {
          // Do nothing while we have no CQE or poll_cq error
        }

        if (unlikely(wc.status != 0)) {
          fprintf(stderr, "eRPC: Fatal error. Bad SEND wc status %d\n",
                  wc.status);
          exit(-1);
        }
      }
    } else {
      flag = 0;
    }

    nb_tx++;
    return flag;
  }

 private:
  /**
   * @brief Resolve fields in \p resolve using \p phy_port
   * @throw runtime_error if the port cannot be resolved
   */
  void resolve_phy_port();

  /**
   * @brief Initialize structures that do not require eRPC hugepages: device
   * context, protection domain, and queue pair.
   *
   * @throw runtime_error if initialization fails
   */
  void init_infiniband_structs();

  /// Initialize the memory registration and deregistration functions
  void init_mem_reg_funcs();

  /**
   * @brief Initialize constant fields of RECV descriptors, fill in the Rpc's
   * RX ring, and fill the RECV queue.
   *
   * @throw runtime_error if RECV buffer hugepage allocation fails
   */
  void init_recvs(uint8_t **rx_ring);

  /// Initialize non-inline SEND buffers and constant fields of SEND descriptors
  void init_sends();

  /// ibverbs info resolved from \p phy_port, must be filled by constructor.
  struct {
    int device_id = -1;  ///< Device index in list of verbs devices
    struct ibv_context *ib_ctx = nullptr;  ///< The verbs device context
    uint8_t dev_port_id = 0;  ///< 1-based port ID in device. 0 is invalid.
  } resolve;

  struct ibv_pd *pd = nullptr;
  struct ibv_qp *send_qp = nullptr;
  struct ibv_cq *send_cq = nullptr;

  struct ibv_qp *recv_qp;
  struct ibv_cq *recv_cq;
  struct ibv_exp_wq *wq;
  struct ibv_exp_wq_family *wq_family;
  struct ibv_exp_rwq_ind_table *ind_tbl;

  Buffer recv_extent;

  // SEND
  size_t nb_tx = 0;  /// Total number of packets sent, reset to 0 on tx_flush()
  struct ibv_send_wr send_wr[kPostlist + 1];  /// +1 for unconditional ->next
  struct ibv_sge send_sgl[kPostlist][2];  ///< SGEs for eRPC header & payload

  // RECV
  size_t recv_head = 0;      ///< Index of current un-posted RECV buffer
  size_t recvs_to_post = 0;  ///< Current number of RECVs to post

  struct ibv_recv_wr recv_wr[kRecvQueueDepth];
  struct ibv_sge recv_sgl[kRecvQueueDepth];
  struct ibv_wc recv_wc[kRecvQueueDepth];
};

}  // End erpc

#endif  // ERPC_RAW_TRANSPORT_H

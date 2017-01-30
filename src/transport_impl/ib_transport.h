#ifndef ERPC_INFINIBAND_TRANSPORT_H
#define ERPC_INFINIBAND_TRANSPORT_H

#include <infiniband/verbs.h>
#include "transport.h"

namespace ERpc {

class IBTransport : public Transport {
  // Transport-specific constants
  static const size_t kMTU = 4096;              ///< InfiniBand MTU
  static const size_t kRecvSize = (kMTU + 64);  ///< RECV buf size (with GRH)
  static const size_t kUnsigBatch = 64;  ///< Selective signaling for SENDs
  static const size_t kPostlist = 16;    ///< Maximum postlist size for SENDs
  static const size_t kMaxInline = 60;   ///< Maximum send wr inline data
  static const size_t kRecvSlack = 32;   ///< RECVs accumulated before posting
  static const uint32_t kQKey = 0xffffffff;  ///< Secure key for all eRPC nodes

  static_assert(kSendQueueDepth >= 2 * kUnsigBatch, ""); /* Capacity check */
  static_assert(kPostlist <= kUnsigBatch, "");           /* Postlist check */

  struct ib_routing_info_t {
    uint16_t port_lid;
    uint32_t qpn;
  };
  static_assert(sizeof(ib_routing_info_t) <= kMaxRoutingInfoSize, "");

 public:
  /// Construct the transport object. Throws \p runtime_error if creation fails.
  /// This exception is caught in the creator Rpc, which then deletes
  /// \p huge_alloc.
  IBTransport(HugeAllocator *huge_alloc, uint8_t phy_port, uint8_t app_tid);
  ~IBTransport();

  void fill_routing_info(RoutingInfo *routing_info) const;

  static std::string routing_info_str(RoutingInfo *routing_info) {
    ib_routing_info_t *ib_routing_info = (ib_routing_info_t *)routing_info;
    std::ostringstream ret;
    ret << "[LID: " << std::to_string(ib_routing_info->port_lid)
        << ", QPN: " << std::to_string(ib_routing_info->qpn) << "]";
    return std::string(ret.str());
  }

  void send_message(Session *session, const Buffer *buffer);
  void poll_completions();

 private:
  /// Fill in \p ib_ctx, \p device_id, and \p dev_port_id using phy_port.
  /// If this function returns, these members are valid.
  void resolve_phy_port();

  /// Initialize device context, queue pairs, memory regions etc
  void init_infiniband_structs();

  /// Initialize RECV buffers and constant fields of RECV descriptors
  void init_recvs();

  /// Initialize non-inline SEND buffers and constant fields of SEND descriptors
  void init_sends();

  // InfiniBand info
  struct ibv_context *ib_ctx = nullptr;
  int device_id = -1;    /* Resolved from \p phy_port */
  int dev_port_id = -1;  /* 1-based, unlike \p phy_port */
  uint16_t port_lid = 0; /* InfiniBand LID of \p phy_port. 0 is invalid. */
  struct ibv_pd *pd = nullptr;
  struct ibv_cq *send_cq = nullptr, *recv_cq = nullptr;
  struct ibv_qp *qp = nullptr;
  uint8_t *recv_extent = nullptr;
  struct ibv_mr *recv_extent_mr = nullptr;
  uint8_t *req_retrans_extent = nullptr;
  struct ibv_mr *req_retrans_mr = nullptr;

  // SEND
  size_t nb_pending = 0;                     /* For selective signalling */
  struct ibv_send_wr send_wr[kPostlist + 1]; /* +1 for blind ->next */
  struct ibv_sge send_sgl[kPostlist];        /* No need for +1 here */

  // RECV
  size_t recv_head = 0;     /* Current un-posted RECV buffer */
  size_t recv_slack = 0;    /* RECVs to accumulate before post_recv() */
  size_t recvs_to_post = 0; /* Current number of RECVs to post */

  struct ibv_recv_wr recv_wr[kRecvQueueDepth];
  struct ibv_sge recv_sgl[kRecvQueueDepth];
  struct ibv_wc wc[kRecvQueueDepth];

  /* Once post_recvs_fast() is used, regular post_recv() must not be used */
  bool fast_recv_used = false;
};

}  // End ERpc

#endif  // ERPC_INFINIBAND_TRANSPORT_H

#ifndef ERPC_INFINIBAND_TRANSPORT_H
#define ERPC_INFINIBAND_TRANSPORT_H

#include <infiniband/verbs.h>
#include "transport.h"

namespace ERpc {

class IBTransport : public Transport {
  // Transport-specific constants
  static const size_t kMTU = 4096;
  static const size_t kRecvBufSize = (kMTU + 64); /* Space for GRH */
  static const size_t kMaxInline = 60;
  static const size_t kRecvSlack = 32;

 public:
  IBTransport(HugeAllocator *huge_alloc, uint8_t phy_port);
  ~IBTransport();

  void fill_routing_info(RoutingInfo *routing_info) const;

  void send_message(Session *session, const Buffer *buffer);
  void poll_completions();

 private:
  /// Fill in ctx, device_id, and dev_port_id using phy_port
  void resolve_phy_port();

  /// Initialize device context, queue pairs, memory regions etc
  void init_infiniband_structs();

	void init_send_wrs();
	void init_recv_wrs();

  // InfiniBand info
  struct ibv_context *ib_ctx = nullptr;
  int device_id = -1; /* Resolved from @phy_port */
  int dev_port_id = -1; /* 1-based, unlike @phy_port */
  struct ibv_qp *qp = nullptr;
  struct ibv_cq *cq = nullptr;
  struct ibv_mr *recv_buf_mr = nullptr, *non_inline_buf_mr = nullptr;
  uint8_t *recv_buf = nullptr, *non_inline_buf = nullptr;

  // SEND
  size_t nb_pending = 0;                          /* For selective signalling */
  struct ibv_send_wr send_wr[kSendQueueSize + 1]; /* +1 for blind ->next */
  struct ibv_sge send_sgl[kSendQueueSize];        /* No need for +1 here */

  // RECV
  size_t recv_head = 0;     /* Current un-posted RECV buffer */
  size_t recv_slack = 0;    /* RECVs to accumulate before post_recv() */
  size_t recvs_to_post = 0; /* Current number of RECVs to post */

  struct ibv_recv_wr recv_wr[kRecvQueueSize];
  struct ibv_sge recv_sgl[kRecvQueueSize];
  struct ibv_wc wc[kRecvQueueSize];

  /* Once post_recvs_fast() is used, regular post_recv() must not be used */
  bool fast_recv_used = false;
};

}  // End ERpc

#endif  // ERPC_INFINIBAND_TRANSPORT_H

#ifndef ERPC_INFINIBAND_TRANSPORT_H
#define ERPC_INFINIBAND_TRANSPORT_H

#include <infiniband/verbs.h>
#include "transport.h"

namespace ERpc {

class InfiniBandTransport : public Transport {
 public:
  InfiniBandTransport(HugeAllocator *huge_alloc, uint8_t phy_port);
  ~InfiniBandTransport();

  void fill_routing_info(RoutingInfo *routing_info) const;

  void send_message(Session *session, const Buffer *buffer);
  void poll_completions();

 private:
	void init_non_zero_members();
	/* Initialize unchanging fields of wr's for performance */
	void init_send_wrs();
	void init_recv_wrs();

  // SEND
  size_t nb_pending = 0;                          /* For selective signalling */
  struct ibv_send_wr send_wr[kSendQueueSize + 1]; /* +1 for blind ->next */
  struct ibv_sge send_sgl[kRecvQueueSize];        /* No need for +1 here */

  // RECV
  size_t recv_step = 0;     /* Step size into dgram_buf for RECV posting */
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

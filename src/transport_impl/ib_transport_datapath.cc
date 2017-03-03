#include "ib_transport.h"

namespace ERpc {
void IBTransport::tx_burst(RoutingInfo const* const* routing_info_arr,
                           MsgBuffer** msg_buffer_arr, size_t num_pkts) {
  assert(routing_info_arr != nullptr);
  assert(msg_buffer_arr != nullptr);
  assert(num_pkts >= 1 && num_pkts <= kPostlist);

  for (size_t i = 0; i < num_pkts; i++) {
    struct ibv_send_wr& wr = send_wr[i];
    struct ibv_sge* sgl = send_sgl[i];

    MsgBuffer* msg_buffer = msg_buffer_arr[i];
    assert(msg_buffer->buf != nullptr);

    /* Data bytes left to send in the message */
    size_t data_bytes_left = (msg_buffer->size - msg_buffer->data_bytes_sent);
    assert(data_bytes_left > 0);

    /* Number of data bytes that will be sent with this work request */
    size_t data_bytes_to_send =
        data_bytes_left < kMaxDataPerPkt ? data_bytes_left : kMaxDataPerPkt;

    /* Verify constant fields of work request */
    assert(wr.next == &send_wr[i + 1]); /* +1 is valid */
    assert(wr.wr.ud.remote_qkey == kQKey);
    assert(wr.opcode == IBV_WR_SEND_WITH_IMM);
    assert(wr.sg_list == sgl);

    /* Set signaling flag. The work request is non-inline by default. */
    wr.send_flags = get_signaled_flag();

    /* Encode variable fields */
    if (msg_buffer->pkts_sent == 0) {
      pkthdr_t* pkthdr = get_pkthdr_0(msg_buffer);

      /* If this is the first packet, we need only 1 SGE */
      sgl[0].addr = (uint64_t)pkthdr;
      sgl[0].length = (uint32_t)(sizeof(pkthdr_t) + data_bytes_to_send);
      sgl[0].lkey = msg_buffer->lkey;

      /* Only single-sge work requests are made inline */
      wr.send_flags |= (sgl[0].length <= kMaxInline) ? IBV_SEND_INLINE : 0;
      wr.num_sge = 1;
    } else {
      pkthdr_t* pkthdr = get_pkthdr_n(msg_buffer, msg_buffer->pkts_sent);

      /* If this is not the first packet, we need 2 SGEs */
      send_sgl[i][0].addr = (uint64_t)pkthdr;
      send_sgl[i][0].length = (uint32_t)sizeof(pkthdr_t);
      send_sgl[i][0].lkey = msg_buffer->lkey;

      send_sgl[i][1].addr =
          (uint64_t) & (msg_buffer->buf[msg_buffer->data_bytes_sent]);
      send_sgl[i][1].length = (uint32_t)data_bytes_to_send;
      send_sgl[i][1].lkey = msg_buffer->lkey;

      /* XXX: Set pkthdr fields */
      pkthdr->is_first = false;

      wr.num_sge = 2;
    }

    auto ib_routing_info = (struct ib_routing_info_t*)routing_info_arr[i];
    wr.wr.ud.remote_qpn = ib_routing_info->qpn;
    wr.wr.ud.ah = &ib_routing_info->ah;
  }

  send_wr[num_pkts - 1].next = nullptr; /* Breaker of chains */

  struct ibv_send_wr* bad_wr;

  /* Handle failure. XXX: Don't exit. */
  int ret = ibv_post_send(qp, &send_wr[0], &bad_wr);
  if (ret != 0) {
    fprintf(stderr, "ibv_post_send failed. ret = %d\n", ret);
    exit(-1);
  }

  send_wr[num_pkts - 1].next = &send_wr[num_pkts]; /* Restore chain; safe */
}

void IBTransport::rx_burst(MsgBuffer* msg_buffer_arr, size_t* num_pkts) {
  assert(msg_buffer_arr != nullptr);
  assert(num_pkts != nullptr);
  _unused(msg_buffer_arr);
  _unused(num_pkts);
}

void IBTransport::post_recvs(size_t num_recvs) {
  assert(num_recvs > 0);
  _unused(num_recvs);
}

}  // End ERpc

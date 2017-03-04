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
    size_t data_bytes_left = (msg_buffer->data_size - msg_buffer->data_sent);
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

    if (msg_buffer->pkts_sent == 0) {
      /* This is the first packet, so we need only 1 SGE */
      pkthdr_t* pkthdr = msg_buffer->get_pkthdr_0();
      sgl[0].addr = (uint64_t)pkthdr;
      sgl[0].length = (uint32_t)(sizeof(pkthdr_t) + data_bytes_to_send);
      sgl[0].lkey = msg_buffer->buffer.lkey;

      /* Only single-sge work requests are inlined */
      wr.send_flags |= (sgl[0].length <= kMaxInline) ? IBV_SEND_INLINE : 0;
      wr.num_sge = 1;
    } else {
      /* This is not the first packet, so we need 2 SGEs */
      pkthdr_t* pkthdr = msg_buffer->get_pkthdr_n(msg_buffer->pkts_sent);
      sgl[0].addr = (uint64_t)pkthdr;
      sgl[0].length = (uint32_t)sizeof(pkthdr_t);
      sgl[0].lkey = msg_buffer->buffer.lkey;

      send_sgl[i][1].addr =
          (uint64_t) & (msg_buffer->buf[msg_buffer->data_sent]);
      send_sgl[i][1].length = (uint32_t)data_bytes_to_send;
      send_sgl[i][1].lkey = msg_buffer->buffer.lkey;

      wr.num_sge = 2;
    }

    auto ib_routing_info = (struct ib_routing_info_t*)routing_info_arr[i];
    wr.wr.ud.remote_qpn = ib_routing_info->qpn;
    wr.wr.ud.ah = &ib_routing_info->ah;

    /* Update MsgBuffer progress tracking metadata */
    msg_buffer->data_sent += data_bytes_to_send;
    msg_buffer->pkts_sent++;
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

void IBTransport::rx_burst(Buffer* buffer_arr, size_t* num_pkts) {
  assert(buffer_arr != nullptr);
  assert(num_pkts != nullptr);
  _unused(buffer_arr);
  _unused(num_pkts);
}

void IBTransport::post_recvs(size_t num_recvs) {
  assert(num_recvs > 0);
  _unused(num_recvs);
}

}  // End ERpc

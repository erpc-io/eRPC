#include "ib_transport.h"

namespace ERpc {
void IBTransport::tx_burst(RoutingInfo const* const* routing_info_arr,
                           MsgBuffer** msg_buffer_arr, size_t num_pkts) {
  assert(routing_info_arr != nullptr);
  assert(msg_buffer_arr != nullptr);
  assert(num_pkts >= 1 && num_pkts <= kPostlist);

  _unused(routing_info_arr);
  _unused(msg_buffer_arr);
  _unused(num_pkts);

  for (size_t i = 0; i < num_pkts; i++) {
    struct ibv_send_wr& wr = send_wr[i];
    MsgBuffer* msg_buffer = msg_buffer_arr[i];
    assert(msg_buffer->size > msg_buffer->data_bytes_sent);

    /* Verify constant fields of work request */
    assert(wr.next == &send_wr[i + 1]); /* +1 is valid */
    assert(wr.wr.ud.remote_qkey == kQKey);
    assert(wr.opcode == IBV_WR_SEND_WITH_IMM);
    assert(wr.sg_list == &send_sgl[i][0]);

    size_t data_bytes_pending =
        (msg_buffer->size - msg_buffer->data_bytes_sent);
    size_t data_bytes_now_sending =
        data_bytes_pending < kMaxDataPerPkt ? msg_buffer->size : kMaxDataPerPkt;

    /* Encode variable fields */
    size_t num_sge;
    if (msg_buffer->data_bytes_sent == 0) {
      /* If this is the first packet, we need only 1 SGE */
      send_sgl[i][0].addr = (uint64_t)msg_buffer_hdr(msg_buffer);
      send_sgl[i][0].length = (uint32_t)data_bytes_now_sending;
      send_sgl[i][0].lkey = msg_buffer->lkey;
      num_sge = 1;
    } else {
      num_sge = 2;
    }

    _unused(num_sge);
  }
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

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
    /* Verify constant fields */
    assert(send_wr[i].next == &send_wr[i + 1]); /* +1 is valid */
    assert(send_wr[i].wr.ud.remote_qkey == kQKey);
    assert(send_wr[i].opcode == IBV_WR_SEND_WITH_IMM);
    assert(send_wr[i].sg_list == &send_sgl[i][0]);

    /* Encode variable fields */
    size_t num_sge;
    _unused(num_sge);
    if (msg_buffer_arr[i]->data_bytes_sent == 0) {
      num_sge = 1;
      send_sgl[i][0].addr = (uint64_t)msg_buffer_hdr(msg_buffer_arr[i]);
    } else {
      num_sge = 2;
    }
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

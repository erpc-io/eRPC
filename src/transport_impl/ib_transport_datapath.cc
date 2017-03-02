#include "ib_transport.h"

namespace ERpc {
void IBTransport::tx_burst(RoutingInfo const* const* routing_info_arr,
                           Buffer const* const* pkt_buffer_arr,
                           size_t const* offset_arr, size_t num_pkts) {
  assert(routing_info_arr != nullptr);
  assert(pkt_buffer_arr != nullptr);
  assert(offset_arr != nullptr);
  assert(num_pkts >= 1 && num_pkts <= kPostlist);

  _unused(routing_info_arr);
  _unused(pkt_buffer_arr);
  _unused(offset_arr);
  _unused(num_pkts);

  for (size_t i = 0; i < num_pkts; i++) {
    auto &wr = send_wr[i];
    _unused(wr);
    /* Verify constant fields */
    assert(wr.next == &send_wr[i + 1]); /* +1 is valid */
    assert(wr.wr.ud.remote_qkey == kQKey);
    assert(wr.opcode == IBV_WR_SEND_WITH_IMM);
    assert(wr.sg_list == &send_sgl[i][0]);

    /* Encode variable fields */
    size_t num_sge;
    _unused(num_sge);
    if (offset_arr[i] == 0) {
      num_sge = 1;
    } else {
      num_sge = 2;
    }
  }
}

void IBTransport::rx_burst(Buffer* pkt_buffer_arr, size_t* num_pkts) {
  assert(pkt_buffer_arr != nullptr);
  assert(num_pkts != nullptr);
  _unused(pkt_buffer_arr);
  _unused(num_pkts);
}

void IBTransport::post_recvs(size_t num_recvs) {
  assert(num_recvs > 0);
  _unused(num_recvs);
}

}  // End ERpc

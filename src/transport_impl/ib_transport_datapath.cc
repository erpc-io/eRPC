#include "ib_transport.h"

namespace ERpc {
void IBTransport::send_packet_batch(RoutingInfo const* const* routing_info_arr,
                                    Buffer const* const* pkt_buffer_arr,
                                    size_t const* offset_arr, size_t num_pkts) {
  _unused(routing_info_arr);
  _unused(pkt_buffer_arr);
  _unused(offset_arr);
  _unused(num_pkts);
}

void IBTransport::poll_completions() {}

}  // End ERpc

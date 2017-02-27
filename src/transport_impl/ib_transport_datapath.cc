#include "ib_transport.h"

namespace ERpc {
void IBTransport::tx_burst(RoutingInfo const* const* routing_info_arr,
                           Buffer const* const* pkt_buffer_arr,
                           size_t const* offset_arr, size_t num_pkts) {
  assert(routing_info_arr != nullptr);
  assert(pkt_buffer_arr != nullptr);
  assert(offset_arr != nullptr);
  assert(num_pkts > 0 && num_pkts <= kPostlist);
}

void IBTransport::rx_burst(Buffer* pkt_buffer_arr, size_t* num_pkts) {
  assert(pkt_buffer_arr != nullptr);
  assert(num_pkts != nullptr);
}

void IBTransport::post_recvs(size_t num_recvs) { assert(num_recvs > 0); }

}  // End ERpc

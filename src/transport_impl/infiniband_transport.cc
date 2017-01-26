#include "infiniband_transport.h"
#include "util/udp_client.h"

namespace ERpc {

InfiniBandTransport::InfiniBandTransport() {
  transport_type = TransportType::kInfiniBand;
}

InfiniBandTransport::~InfiniBandTransport() {}

void InfiniBandTransport::fill_routing_info(RoutingInfo *routing_info) const {
  memset((void *)routing_info, 0, kMaxRoutingInfoSize);
  return;
}

void InfiniBandTransport::send_message(Session *session, const Buffer *buffer) {
  _unused(session);
  _unused(buffer);
}

void InfiniBandTransport::poll_completions() {}
};

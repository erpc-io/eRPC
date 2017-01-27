#include "ib_transport.h"
#include "util/udp_client.h"

namespace ERpc {


IBTransport::IBTransport(HugeAllocator *huge_alloc,
                                         uint8_t phy_port)
    : Transport(kInfiniBandMTU, TransportType::kInfiniBand, phy_port,
                huge_alloc) {
}

IBTransport::~IBTransport() {
  /* Do not destroy @huge_alloc; the parent Rpc will do it. */
}

void IBTransport::fill_routing_info(RoutingInfo *routing_info) const {
  memset((void *)routing_info, 0, kMaxRoutingInfoSize);
  return;
}

void IBTransport::send_message(Session *session, const Buffer *buffer) {
  _unused(session);
  _unused(buffer);
}

void IBTransport::poll_completions() {}
};

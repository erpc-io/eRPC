#include "infiniband_transport.h"
#include "util/udp_client.h"

namespace ERpc {

static const size_t kInfiniBandMTU = 4096;

InfiniBandTransport::InfiniBandTransport(HugeAllocator *huge_alloc,
                                         uint8_t phy_port) :
    Transport(kInfiniBandMTU, TransportType::kInfiniBand, phy_port, huge_alloc) {
}

InfiniBandTransport::~InfiniBandTransport() {
  /* Do not destroy @huge_alloc; the parent Rpc will do it. */
}

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

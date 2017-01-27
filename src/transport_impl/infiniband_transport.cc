#include "infiniband_transport.h"
#include "util/udp_client.h"

namespace ERpc {

InfiniBandTransport::InfiniBandTransport(HugeAllocator *_huge_alloc) {
  /* These are members of the base class, so can't use initializer list here */
  transport_type = TransportType::kInfiniBand;
  huge_alloc = _huge_alloc;
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

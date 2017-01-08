#include <infiniband/verbs.h>
#include "transport.h"
#include "util/udp_client.h"

namespace ERpc {

InfiniBandTransport::InfiniBandTransport() {
  transport_type = TransportType::kInfiniBand;
}

InfiniBandTransport::~InfiniBandTransport() {}

void InfiniBandTransport::send_resolve_session_msg(Session *session) const {
  _unused(session);
  return;
}

void InfiniBandTransport::send_message(Session *session, const Buffer *buffer) {
  _unused(session);
  _unused(buffer);
}

void InfiniBandTransport::poll_completions() {}
};

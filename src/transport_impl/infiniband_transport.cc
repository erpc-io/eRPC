#include <infiniband/verbs.h>
#include "transport.h"

namespace ERpc {

InfiniBandTransport::InfiniBandTransport() {
  transport_type = TransportType::InfiniBand;
}

InfiniBandTransport::~InfiniBandTransport() {}

void InfiniBandTransport::resolve_session(Session &session) {
  _unused(session);
  return;
}

void InfiniBandTransport::send_message(Session &session, const Buffer &buffer) {
  int rem_qpn = session.rem_qpn;
  struct ibv_ah *rem_ah = session.rem_ah;

  _unused(rem_qpn);
  _unused(rem_ah);
  _unused(session);
  _unused(buffer);
}

void InfiniBandTransport::poll_completions() {}
};

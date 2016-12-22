#ifndef ERPC_INFINIBAND_H_H
#define ERPC_INFINIBAND_H_H

#include "common.h"
#include "session.h"
#include "transport.h"

#include <infiniband/verbs.h>
#include <stdio.h>
#include <string.h>

namespace ERpc {
class InfinibandTransport : public Transport {
public:
  InfinibandTransport() { type = TransportType::InfiniBand; }

  ~InfinibandTransport();

  void resolve_session(Session &session) {
    _unused(session);
    return;
  }

  void send_message(Session *session) {
    int rem_qpn = session->rem_qpn;
    struct ibv_ah *rem_ah = session->rem_ah;

    _unused(rem_qpn);
    _unused(rem_ah);
    _unused(session);
  }

  void poll_completions() {}
};
}

#endif // ERPC_INFINIBAND_H_H

#ifndef ERPC_TRANSPORT_H
#define ERPC_TRANSPORT_H

#include "common.h"
#include "session.h"
#include "transport_types.h"
#include "util/buffer.h"

namespace ERpc {

// Generic unreliable transport class
class Transport {
 public:
  /**
   * @brief Resolve the transport-specific fields of \p session by talking
   * to the remote host.
   */
  void send_resolve_session_msg(Session &session) const;

  void send_message(Session &session, const Buffer &buffer);
  void poll_completions();

  TransportType transport_type;
};

class InfiniBandTransport : public Transport {
 public:
  InfiniBandTransport();
  ~InfiniBandTransport();

  void send_resolve_session_msg(Session &session) const;

  void send_message(Session &session, const Buffer &buffer);
  void poll_completions();

  TransportType transport_type;
};

}  // End ERpc

#endif  // ERPC_TRANSPORT_H

#ifndef ERPC_TRANSPORT_H
#define ERPC_TRANSPORT_H

#include "buffer.h"
#include "common.h"
#include "session.h"
#include "transport_types.h"

namespace ERpc {

// Generic unreliable transport class
class Transport {
 public:
  Transport();
  ~Transport();

  /**
   * @brief Resolve the transport-specific fields of \p session by talking
   * to the remote host.
   */
  virtual void resolve_session(Session &session);

  virtual void send_message(Session &session, Buffer *buffer);
  virtual void poll_completions();

  TransportType type;
};

}  // End ERpc

#endif  // ERPC_TRANSPORT_H

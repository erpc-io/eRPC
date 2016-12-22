#ifndef ERPC_TRANSPORT_H
#define ERPC_TRANSPORT_H

#include "common.h"
#include "session.h"
#include <strings.h>

namespace ERpc {

enum TransportType {
  InfiniBand,
  RoCE,
  OmniPath,
  Invalid
};

TransportType get_type(const char* transport_type) {
  if (strcasecmp(transport_type, "InfiniBand")) {
    return TransportType::InfiniBand;
  } else if (strcasecmp(transport_type, "RoCE")) {
    return TransportType::RoCE;
  } else if (strcasecmp(transport_type, "OmniPath")) {
    return TransportType::OmniPath;
  } else {
    return TransportType::Invalid;
  }
}

// Generic transport class
class Transport {
public:
  Transport();
  ~Transport();

  /**
   * @brief Fill in the transport-related fields of \p session based on
   * its remote_port_name.
   */
  virtual void resolve_session(Session &session);
  virtual void send_message(Session *session);
  virtual void poll_completions();

  TransportType type;
};

} // End ERpc

#endif // ERPC_TRANSPORT_H

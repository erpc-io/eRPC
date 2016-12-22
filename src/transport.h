#ifndef ERPC_TRANSPORT_H
#define ERPC_TRANSPORT_H

#include "common.h"
#include "session.h"

namespace ERpc {

enum TransportType {
  InfiniBand,
  RoCE,
  OmniPath,
};

// Generic transport class
class Transport {
public:
  Transport();
  ~Transport();

  virtual void send_message(Session *session);
  virtual void poll_completions();

  TransportType type;
};

} // End ERpc

#endif //ERPC_TRANSPORT_H

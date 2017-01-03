#ifndef ERPC_RPC_H
#define ERPC_RPC_H

#include "buffer.h"
#include "common.h"
#include "session.h"
#include "transport.h"

namespace ERpc {

class Rpc {
 public:
  Rpc(Transport transport) : transport(transport){};
  ~Rpc();

  void send_request(Session &session, Buffer &buffer);
  void send_response(Session &session, Buffer &buffer);

  void run_event_loop();

  Transport transport;
};

}  // End ERpc

#endif  // ERPC_RPC_H

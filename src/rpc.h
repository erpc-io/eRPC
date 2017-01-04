#ifndef ERPC_RPC_H
#define ERPC_RPC_H

#include "common.h"
#include "nexus.h"
#include "session.h"
#include "transport.h"
#include "util/buffer.h"

namespace ERpc {

// Per-thread RPC object
template <class Transport>
class Rpc {
 public:
  Rpc(Nexus &nexus);
  ~Rpc();

  void send_request(const Session &session, const Buffer &buffer);
  void send_response(const Session &session, const Buffer &buffer);

  void run_event_loop();

 private:
  Nexus &nexus;
  Transport &transport; /* The unreliable transport */
  const volatile SessionManagementHook sm_hook;
};

}  // End ERpc

#endif  // ERPC_RPC_H

#ifndef ERPC_RPC_H
#define ERPC_RPC_H

#include "common.h"
#include "nexus.h"
#include "session.h"
#include "transport.h"
#include "util/buffer.h"

namespace ERpc {

#define rpc_dprintf(fmt, ...)            \
  do {                                   \
    if (RPC_DPRINTF) {                   \
      fprintf(stderr, fmt, __VA_ARGS__); \
      fflush(stderr);                    \
    }                                    \
  } while (0)

// Per-thread RPC object
template <class Transport_>
class Rpc {
 public:
  Rpc(Nexus &nexus);
  ~Rpc();

  void send_request(const Session &session, const Buffer &buffer);
  void send_response(const Session &session, const Buffer &buffer);

  void run_event_loop();

 private:
  Nexus &nexus;
  Transport_ *transport; /* The unreliable transport */

  /*
   * The Nexus modifies this object when there is session-management related
   * work to be done.
   */
  const volatile SessionManagementHook sm_hook;
};

/* Instantiate required Rpc classes so they get compiled for the linker */
template class Rpc<InfiniBandTransport>;

}  // End ERpc

#endif  // ERPC_RPC_H

#ifndef ERPC_RPC_H
#define ERPC_RPC_H

#include <mutex>
#include <queue>
#include "util/buffer.h"
#include "common.h"
#include "session.h"
#include "transport.h"
using namespace std;

namespace ERpc {

class Nexus; // Forward declaration

// Per-thread RPC object
class Rpc {
 public:
  Rpc(Nexus &nexus, Transport &transport);
  ~Rpc();

  void send_request(const Session &session, const Buffer &buffer);
  void send_response(const Session &session, const Buffer &buffer);

  void run_event_loop();

 private:
  Nexus &nexus;
  Transport &transport; /* The unreliable transport */

  /* Session establishment req/resp queues that the Nexus inserts into */
  std::mutex session_mgmt_mutex;
  std::queue<SessionEstablishmentReq> session_req_queue;
  std::queue<SessionEstablishmentResp> session_resp_queue;
};

}  // End ERpc

#endif  // ERPC_RPC_H

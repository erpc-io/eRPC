#include "session.h"

namespace ERpc {

const size_t Session::kSessionReqWindow;
const size_t Session::kSessionCredits;

Session::Session(Role role, SessionState state) : role(role), state(state) {
  // A session may be created by the client in kConnectInProgress state, or
  // by the server in kConnected statet.
  if (is_client()) {
    assert(state == SessionState::kConnectInProgress);
    remote_routing_info = &server.routing_info;
  } else {
    assert(state == SessionState::kConnected);
    remote_routing_info = &client.routing_info;
  }

  // Arrange the free slot vector so that slots are popped in order
  for (size_t i = 0; i < kSessionReqWindow; i++) {
    size_t sslot_i = (kSessionReqWindow - 1 - i);
    SSlot &sslot = sslot_arr[sslot_i];

    sslot.session = this;
    sslot.index = sslot_i;
    sslot.max_rx_req_num = 0;
    sslot.rx_msgbuf.buffer.buf = nullptr;  // Bury rx_msgbuf
    sslot.rx_msgbuf.buf = nullptr;         // Bury rx_msgbuf
    sslot.tx_msgbuf = nullptr;             // Bury tx_msgbuf

    sslot.prealloc_used = true;  // There's no user-allocated memory to free
    sslot.client_info.cont_etid = kInvalidBgETid;  // Continuations in fg

    client_info.sslot_free_vec.push_back(sslot_i);
  }
}

}  // End ERpc

#include "session.h"

namespace ERpc {

constexpr size_t Session::kSessionReqWindow;
constexpr size_t Session::kSessionCredits;

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
    // Initialize session slot with index = sslot_i
    size_t sslot_i = (kSessionReqWindow - 1 - i);
    SSlot &sslot = sslot_arr[sslot_i];

    sslot.session = this;
    sslot.is_client = this->is_client();
    sslot.index = sslot_i;

    // The first request number for this sslot is (sslot_i + kSessionReqWindow)
    sslot.cur_req_num = sslot_i;

    // Bury MsgBuffers
    sslot.tx_msgbuf = nullptr;
    if (this->is_client()) {
      sslot.client_info.resp_msgbuf = nullptr;
    } else {
      sslot.server_info.req_msgbuf.buf = nullptr;
      sslot.server_info.req_msgbuf.buffer.buf = nullptr;
    }

    sslot.prealloc_used = true;  // There's no user-allocated memory to free
    sslot.client_info.cont_etid = kInvalidBgETid;  // Continuations in fg

    sslot.server_info.req_rcvd = 0;
    sslot.server_info.rfr_rcvd = 0;

    client_info.sslot_free_vec.push_back(sslot_i);
  }
}

}  // End ERpc

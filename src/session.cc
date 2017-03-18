#include "session.h"

namespace ERpc {

const size_t Session::kSessionReqWindow;
const size_t Session::kSessionCredits;

Session::Session(Role role, SessionState state) : role(role), state(state) {
  /*
   * A session may be created by the client in kConnectInProgress state, or
   * by the server in kConnected statet.
   */
  if (is_client()) {
    assert(state == SessionState::kConnectInProgress);
    remote_routing_info = &server.routing_info;
  } else {
    assert(state == SessionState::kConnected);
    remote_routing_info = &client.routing_info;
  }

  /* Arrange the free slot vector so that slots are popped in order */
  for (size_t i = 0; i < kSessionReqWindow; i++) {
    size_t sslot_i = (kSessionReqWindow - 1 - i);
    sslot_t &sslot = sslot_arr[sslot_i];
    sslot.req_type = kInvalidReqType;
    sslot.req_num = kInvalidReqNum;
    sslot.rx_msgbuf.buffer.buf = nullptr; /* Invalidate the Buffer */
    sslot.rx_msgbuf.buf = nullptr;        /* Invalid the MsgBuffer */
    sslot.tx_msgbuf = nullptr;
    sslot_free_vec.push_back(sslot_i);

    sslot.app_resp.dyn_resp_msgbuf.buf = nullptr;
  }
}

}  // End ERpc

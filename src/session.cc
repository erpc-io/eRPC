#include "session.h"

namespace ERpc {

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
    size_t index = kSessionReqWindow - 1 - i;
    sslot_arr[index].in_free_vec = true;
    sslot_free_vec.push_back(kSessionReqWindow - 1 - i);
  }
}

}  // End ERpc

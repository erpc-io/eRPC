#include "session.h"

namespace ERpc {

Session::Session(Role role, SessionState state) : role(role), state(state) {
  /*
   * A session may be created by the client in kConnectInProgress state, or
   * by the server in kConnected statet.
   */
  if (role == Session::Role::kClient) {
    assert(state == SessionState::kConnectInProgress);
    remote_routing_info = &server.routing_info;
  } else {
    assert(state == SessionState::kConnected);
    remote_routing_info = &client.routing_info;
  }

  for (size_t i = 0; i < kSessionReqWindow; i++) {
    msg_arr_free_vec.push_back(i);
  }
}

}  // End ERpc

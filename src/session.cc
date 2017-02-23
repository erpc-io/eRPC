#include "session.h"

namespace ERpc {

Session::Session(Role role, SessionState state) : role(role), state(state) {
  /*
   * A session may be created by the client in kConnectInProgress state, or
   * by the server in kConnected statet.
   */
  if (role == Session::Role::kClient) {
    assert(state == SessionState::kConnectInProgress);
  } else {
    assert(state == SessionState::kConnected);
  }

  for (size_t i = 0; i < kSessionReqWindow; i++) {
    msg_arr_free_vec.push_back(i);
  }
}

}  // End ERpc

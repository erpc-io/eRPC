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
}

Session::~Session(){};

}  // End ERpc

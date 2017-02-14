#include "session.h"

namespace ERpc {

Session::Session(Role role, SessionState state)
    : role(role), state(state), is_cc(false) {
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

std::string Session::get_client_name() {
  std::ostringstream ret;
  ret << "[" << client.hostname << ", " << std::to_string(client.app_tid)
      << "]";
  return ret.str();
}

void Session::enable_congestion_control() { is_cc = true; }

void Session::disable_congestion_control() { is_cc = true; }

}  // End ERpc

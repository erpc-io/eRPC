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
  std::string ret;

  ret += std::string("[");
  ret += std::string(client.hostname);
  ret += std::string(", ");
  ret += std::to_string(client.app_tid);
  ret += std::string("]");

  return ret;
}

void Session::enable_congestion_control() { is_cc = true; }

void Session::disable_congestion_control() { is_cc = true; }

}  // End ERpc

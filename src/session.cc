#include "session.h"
#include "pkthdr.h"

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
    sslot.in_free_vec = true; /* Other fields are garbage */
    sslot.req_num = 0;
    sslot_free_vec.push_back(sslot_i);
  }
}

}  // End ERpc

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

    // This buries all MsgBuffers
    memset(static_cast<void *>(&sslot), 0, sizeof(SSlot));

    sslot.prealloc_used = true;  // There's no user-allocated memory to free
    sslot.session = this;
    sslot.is_client = this->is_client();
    sslot.index = sslot_i;
    sslot.cur_req_num = sslot_i;  // 1st req num = (sslot_i + kSessionReqWindow)

    if (sslot.is_client) {
      sslot.client_info.cont_etid = kInvalidBgETid;  // Continuations in fg
    } else {
      sslot.server_info.req_type = kInvalidReqType;
    }

    client_info.sslot_free_vec.push_back(sslot_i);
  }
}

}  // End ERpc

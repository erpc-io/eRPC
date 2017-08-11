/**
 * @file rpc_reset_handler.cc
 * @brief Handler for ENet reset event
 */
#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::handle_reset_st(const std::string reset_rem_hostname) {
  assert(in_creator());
  LOG_INFO("eRPC Rpc %u: Received reset event for remote hostname = %s.\n",
           rpc_id, reset_rem_hostname.c_str());

  for (Session *session : session_vec) {
    if (session == nullptr) continue;

    // Disconnected sessions are immediately nullified
    assert(session->state == SessionState::kDisconnected);

    std::string session_rem_hostname = session->is_client()
                                           ? session->client.hostname
                                           : session->server.hostname;
    if (session_rem_hostname != reset_rem_hostname) continue;

    LOG_WARN(
        "eRPC Rpc %u: Resetting %s session, session number = %u, state = %s.",
        rpc_id, session->is_client() ? "client" : "server",
        session->local_session_num, session_state_str(session->state).c_str());
  }
}

}  // End ERpc

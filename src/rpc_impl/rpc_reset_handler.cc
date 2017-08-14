/**
 * @file rpc_reset_handler.cc
 * @brief Handler for ENet reset event
 */
#include "rpc.h"

namespace ERpc {

template <class TTr>
bool Rpc<TTr>::handle_reset_st(const std::string reset_rem_hostname) {
  assert(in_creator());
  LOG_INFO("eRPC Rpc %u: Handling reset event for remote hostname = %s.\n",
           rpc_id, reset_rem_hostname.c_str());

  // Drain the TX batch and flush the transport
  if (tx_batch_i > 0) do_tx_burst_st();
  transport->tx_flush();

  bool success_all = true;

  for (Session *session : session_vec) {
    if (session == nullptr) continue;

    std::string session_rem_hostname = session->is_client()
                                           ? session->client.hostname
                                           : session->server.hostname;
    if (session_rem_hostname != reset_rem_hostname) continue;

    LOG_WARN(
        "eRPC Rpc %u: Resetting %s session, session number = %u, state = %s.",
        rpc_id, session->is_client() ? "client" : "server",
        session->local_session_num, session_state_str(session->state).c_str());

    session->state = SessionState::kDisconnectInProgress;  // Drop all RX pkts

    bool success_one;
    if (session->is_client()) {
      success_one = handle_reset_client_st(session);
    } else {
      success_one = handle_reset_server_st(session);
    }

    success_all &= success_one;
  }

  return success_all;
}

template <class TTr>
bool Rpc<TTr>::handle_reset_client_st(Session *session) {
  assert(in_creator());
  assert(session != nullptr && session->is_client());

  // Erase session slots from request TX queue
  return true;
}

template <class TTr>
bool Rpc<TTr>::handle_reset_server_st(Session *session) {
  assert(in_creator());
  assert(session != nullptr && session->is_server());
  return true;
}

}  // End ERpc

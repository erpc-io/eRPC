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
    // Filter sessions connected to the reset hostname
    if (session == nullptr) continue;

    if (session->is_client()) {
      if (session->server.hostname != reset_rem_hostname) continue;
    } else {
      if (session->client.hostname != reset_rem_hostname) continue;
    }

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
  for (const SSlot &sslot : session->sslot_arr) {
    req_txq.erase(std::remove(req_txq.begin(), req_txq.end(), &sslot),
                  req_txq.end());
  }

  // Invoke continuation-with-failure for sslots with pending requests
  for (SSlot &sslot : session->sslot_arr) {
    if (sslot.tx_msgbuf != nullptr) {
      sslot.tx_msgbuf = nullptr;  // Invoke continuation-with-failure only once

      // sslot contains a valid request
      MsgBuffer *resp_msgbuf = sslot.client_info.resp_msgbuf;
      assert(resp_msgbuf != nullptr && resp_msgbuf->buf != nullptr &&
             resp_msgbuf->check_magic() && resp_msgbuf->is_dynamic());
      assert(sslot.client_info.cont_func != nullptr);

      resize_msg_buffer(resp_msgbuf, 0);  // 0 response size marks the error
      sslot.client_info.cont_func(static_cast<RespHandle *>(&sslot), context,
                                  sslot.client_info.tag);
    }
  }

  // Return true iff all continuations have called enqueue_response(p
  return session->client_info.sslot_free_vec.size() ==
         Session::kSessionReqWindow;
}

template <class TTr>
bool Rpc<TTr>::handle_reset_server_st(Session *session) {
  assert(in_creator());
  assert(session != nullptr && session->is_server());
  return true;
}

}  // End ERpc

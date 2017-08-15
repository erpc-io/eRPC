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

    auto session_rem_hostname = session->is_client() ? session->server.hostname
                                                     : session->client.hostname;
    if (session_rem_hostname != reset_rem_hostname) continue;

    bool success_one;
    if (session->is_client()) {
      success_one = handle_reset_client_st(session);
    } else {
      success_one = handle_reset_server_st(session);
    }

    // The handler must mark session unconnected so that RX pkts will be dropped
    if (!success_one) assert(session->state == SessionState::kResetInProgress);

    success_all &= success_one;
  }

  return success_all;
}

template <class TTr>
bool Rpc<TTr>::handle_reset_client_st(Session *session) {
  assert(in_creator());
  assert(session != nullptr && session->is_client());

  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %u: Attempting to reset client session %u, state = %s."
          "Issue",
          rpc_id, session->local_session_num,
          session_state_str(session->state).c_str());

  // The session can be in any state, except the temporary disconnected state.
  // In the connected state, the session may have outstanding requests.
  if (session->is_connected()) {
    // Erase session slots from request TX queue
    for (const SSlot &sslot : session->sslot_arr) {
      req_txq.erase(std::remove(req_txq.begin(), req_txq.end(), &sslot),
                    req_txq.end());
    }

    // Invoke continuation-with-failure for sslots with pending requests
    for (SSlot &sslot : session->sslot_arr) {
      if (sslot.tx_msgbuf != nullptr) {
        sslot.tx_msgbuf = nullptr;  // Invoke failure continuation only once

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
  }

  // We can free the session after all continutions call enqueue_response()
  size_t pending_conts =
      Session::kSessionReqWindow - session->client_info.sslot_free_vec.size();

  if (session->state == SessionState::kConnectInProgress ||
      session->state == SessionState::kDisconnectInProgress) {
    assert(pending_conts == 0);
  }

  if (pending_conts == 0) {
    // Act similar to handling a disconnect response
    LOG_INFO("%s: None. Session resetted.\n", issue_msg);
    session->state = SessionState::kDisconnected;  // Temporary state
    sm_handler(session->local_session_num, SmEventType::kDisconnected,
               SmErrType::kSrvDisconnected, context);
    bury_session_st(session);

    return true;
  } else {
    LOG_WARN(
        "eRPC Rpc %u: Cannot reset session %u. %zu continuations pending.\n",
        rpc_id, session->local_session_num, pending_conts);

    session->state = SessionState::kResetInProgress;
    return false;
  }
}

template <class TTr>
bool Rpc<TTr>::handle_reset_server_st(Session *session) {
  assert(in_creator());
  assert(session != nullptr && session->is_server());
  return true;
}

}  // End ERpc

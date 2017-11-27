/**
 * @file rpc_reset_handlers.cc
 * @brief Handlers for session resets
 */
#include "rpc.h"

namespace erpc {

template <class TTr>
bool Rpc<TTr>::handle_reset_st(const std::string reset_rem_hostname) {
  assert(in_dispatch());
  LOG_INFO("eRPC Rpc %u: Handling reset event for remote hostname = %s.\n",
           rpc_id, reset_rem_hostname.c_str());

  // Drain the TX batch and flush the transport
  if (tx_batch_i > 0) do_tx_burst_st();
  transport->tx_flush();

  bool success_all = true;

  for (Session *session : session_vec) {
    // Filter sessions connected to the reset hostname
    if (session == nullptr) continue;

    bool success_one;
    if (session->is_client()) {
      if (session->server.hostname != reset_rem_hostname) continue;
      success_one = handle_reset_client_st(session);
    } else {
      if (session->client.hostname != reset_rem_hostname) continue;
      success_one = handle_reset_server_st(session);
    }

    // If reset succeeds, the session is freed
    if (!success_one) assert(session->state == SessionState::kResetInProgress);
    success_all &= success_one;
  }

  return success_all;
}

template <class TTr>
bool Rpc<TTr>::handle_reset_client_st(Session *session) {
  assert(in_dispatch());
  assert(session->is_client());

  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %u: Trying to reset client session %u, state = %s. Issue",
          rpc_id, session->local_session_num,
          session_state_str(session->state).c_str());

  // The session can be in any state (except the temporary disconnected state).
  // In the connected state, the session may have outstanding requests.
  if (session->is_connected()) {
    // Erase session slots from request TX queue
    for (const SSlot &sslot : session->sslot_arr) {
      req_txq.erase(std::remove(req_txq.begin(), req_txq.end(), &sslot),
                    req_txq.end());
    }

    // Invoke continuation-with-failure for sslots without complete response
    for (SSlot &sslot : session->sslot_arr) {
      if (sslot.tx_msgbuf != nullptr) {
        sslot.tx_msgbuf = nullptr;  // Invoke failure continuation only once

        // sslot contains a valid request
        MsgBuffer *resp_msgbuf = sslot.client_info.resp_msgbuf;
        assert(resp_msgbuf->is_valid_dynamic());
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

  // Change state before failure continuations
  session->state = SessionState::kDisconnectInProgress;

  if (pending_conts == 0) {
    // Act similar to handling a disconnect response
    LOG_INFO("%s: None. Session resetted.\n", issue_msg);
    free_recvs();  // Free before SM callback to allow creating a new session
    sm_handler(session->local_session_num, SmEventType::kDisconnected,
               SmErrType::kSrvDisconnected, context);
    bury_session_st(session);
    return true;
  } else {
    LOG_WARN(
        "eRPC Rpc %u: Cannot reset session %u. %zu continuations pending.\n",
        rpc_id, session->local_session_num, pending_conts);
    return false;
  }
}

template <class TTr>
bool Rpc<TTr>::handle_reset_server_st(Session *session) {
  assert(in_dispatch());
  assert(session->is_server());
  session->state = SessionState::kResetInProgress;

  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %u: Trying to reset server session %u, state = %s. Issue",
          rpc_id, session->local_session_num,
          session_state_str(session->state).c_str());

  size_t pending_enqueue_resps = 0;
  for (const SSlot &sslot : session->sslot_arr) {
    if (sslot.server_info.req_type != kInvalidReqType) pending_enqueue_resps++;
  }

  if (pending_enqueue_resps == 0) {
    // Act similar to handling a disconnect request, but don't send SM response
    LOG_INFO("%s: None. Session resetted.\n", issue_msg);
    free_recvs();
    bury_session_st(session);
    return true;
  } else {
    LOG_WARN(
        "eRPC Rpc %u: Cannot reset session %u. %zu enqueue_response pending.\n",
        rpc_id, session->local_session_num, pending_enqueue_resps);

    return false;
  }
}

}  // End erpc

/**
 * @file rpc_reset_handlers.cc
 * @brief Handlers for session resets
 */
#include "rpc.h"

namespace erpc {

template <class TTr>
bool Rpc<TTr>::handle_reset_st(const std::string reset_rem_hostname) {
  assert(in_dispatch());
  LOG_INFO("Rpc %u: Handling reset event for remote hostname = %s.\n", rpc_id,
           reset_rem_hostname.c_str());

  drain_tx_batch_and_dma_queue();

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
          "Rpc %u, lsn %u: Trying to reset client session. State = %s. Issue",
          rpc_id, session->local_session_num,
          session_state_str(session->state).c_str());

  // The session can be in any state (except the temporary disconnected state).
  // In the connected state, the session may have outstanding requests.
  if (session->is_connected()) {
    // Erase session slots from credit stall queue
    for (const SSlot &sslot : session->sslot_arr) {
      stallq.erase(std::remove(stallq.begin(), stallq.end(), &sslot),
                   stallq.end());
    }

    // Invoke continuation-with-failure for sslots without complete response
    for (SSlot &sslot : session->sslot_arr) {
      if (sslot.tx_msgbuf != nullptr) {
        sslot.tx_msgbuf = nullptr;  // Invoke failure continuation only once

        // sslot contains a valid request
        MsgBuffer *resp_msgbuf = sslot.client_info.resp_msgbuf;
        resize_msg_buffer(resp_msgbuf, 0);  // 0 response size marks the error
        sslot.client_info.cont_func(static_cast<RespHandle *>(&sslot), context,
                                    sslot.client_info.tag);
      }
    }
  }

  // We can free the session after all continutions call enqueue_response()
  size_t pending_conts =
      kSessionReqWindow - session->client_info.sslot_free_vec.size();

  if (session->state == SessionState::kConnectInProgress ||
      session->state == SessionState::kDisconnectInProgress) {
    assert(pending_conts == 0);
  }

  // Change state before failure continuations
  session->state = SessionState::kDisconnectInProgress;

  if (pending_conts == 0) {
    // Act similar to handling a disconnect response
    LOG_INFO("%s: None. Session resetted.\n", issue_msg);
    free_ring_entries();  // Free before callback to allow creating new session
    sm_handler(session->local_session_num, SmEventType::kDisconnected,
               SmErrType::kSrvDisconnected, context);
    bury_session_st(session);
    return true;
  } else {
    LOG_WARN(
        "Rpc %u, lsn %u: Cannot reset client session. %zu conts pending.\n",
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
          "Rpc %u, lsn %u: Trying to reset server session. State = %s. Issue",
          rpc_id, session->local_session_num,
          session_state_str(session->state).c_str());

  size_t pending_enqueue_resps = 0;
  for (const SSlot &sslot : session->sslot_arr) {
    if (sslot.server_info.req_type != kInvalidReqType) pending_enqueue_resps++;
  }

  if (pending_enqueue_resps == 0) {
    // Act similar to handling a disconnect request, but don't send SM response
    LOG_INFO("%s: None. Session resetted.\n", issue_msg);
    free_ring_entries();
    bury_session_st(session);
    return true;
  } else {
    LOG_WARN(
        "Rpc %u, lsn %u: Cannot reset server session. "
        "%zu enqueue_response pending.\n",
        rpc_id, session->local_session_num, pending_enqueue_resps);

    return false;
  }
}

FORCE_COMPILE_TRANSPORTS

}  // End erpc

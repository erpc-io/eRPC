/**
 * @file rpc_reset_handlers.cc
 * @brief Handlers for session resets
 */
#include "rpc.h"

namespace erpc {

template <class TTr>
bool Rpc<TTr>::handle_reset_client_st(Session *session) {
  assert(in_dispatch());

  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "Rpc %u, lsn %u: Trying to reset client session. State = %s. Issue",
          rpc_id, session->local_session_num,
          session_state_str(session->state).c_str());

  // Erase session slots from credit stall queue
  for (const SSlot &sslot : session->sslot_arr) {
    stallq.erase(std::remove(stallq.begin(), stallq.end(), &sslot),
                 stallq.end());
  }

  // Invoke continuation-with-failure for all active requests
  for (SSlot &sslot : session->sslot_arr) {
    if (sslot.tx_msgbuf == nullptr) {
      sslot.tx_msgbuf = nullptr;
      delete_from_active_rpc_list(sslot);
      session->client_info.sslot_free_vec.push_back(sslot.index);

      MsgBuffer *resp_msgbuf = sslot.client_info.resp_msgbuf;
      resize_msg_buffer(resp_msgbuf, 0);  // 0 response size marks the error
      sslot.client_info.cont_func(context, sslot.client_info.tag);
    }
  }

  assert(session->client_info.sslot_free_vec.size() == kSessionReqWindow);

  // Change state before failure continuations
  session->state = SessionState::kDisconnectInProgress;

  // Act similar to handling a disconnect response
  ERPC_INFO("%s: None. Session resetted.\n", issue_msg);
  free_ring_entries();  // Free before callback to allow creating new session
  sm_handler(session->local_session_num, SmEventType::kDisconnected,
             SmErrType::kSrvDisconnected, context);
  bury_session_st(session);
  return true;
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
    ERPC_INFO("%s: None. Session resetted.\n", issue_msg);
    free_ring_entries();
    bury_session_st(session);
    return true;
  } else {
    ERPC_WARN(
        "Rpc %u, lsn %u: Cannot reset server session. "
        "%zu enqueue_response pending.\n",
        rpc_id, session->local_session_num, pending_enqueue_resps);

    return false;
  }
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

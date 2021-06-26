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
          rpc_id_, session->local_session_num_,
          session_state_str(session->state_).c_str());

  // Erase session slots from credit stall queue
  for (const SSlot &sslot : session->sslot_arr_) {
    stallq_.erase(std::remove(stallq_.begin(), stallq_.end(), &sslot),
                  stallq_.end());
  }

  // Invoke continuation-with-failure for all active requests
  for (SSlot &sslot : session->sslot_arr_) {
    if (sslot.tx_msgbuf_ == nullptr) {
      sslot.tx_msgbuf_ = nullptr;
      delete_from_active_rpc_list(sslot);
      session->client_info_.sslot_free_vec_.push_back(sslot.index_);

      MsgBuffer *resp_msgbuf = sslot.client_info_.resp_msgbuf_;
      resize_msg_buffer(resp_msgbuf, 0);  // 0 response size marks the error
      sslot.client_info_.cont_func_(context_, sslot.client_info_.tag_);
    }
  }

  assert(session->client_info_.sslot_free_vec_.size() == kSessionReqWindow);

  // Change state before failure continuations
  session->state_ = SessionState::kDisconnectInProgress;

  // Act similar to handling a disconnect response
  ERPC_INFO("%s: None. Session resetted.\n", issue_msg);
  free_ring_entries();  // Free before callback to allow creating new session
  sm_handler_(session->local_session_num_, SmEventType::kDisconnected,
              SmErrType::kSrvDisconnected, context_);
  bury_session_st(session);
  return true;
}

template <class TTr>
bool Rpc<TTr>::handle_reset_server_st(Session *session) {
  assert(in_dispatch());
  assert(session->is_server());
  session->state_ = SessionState::kResetInProgress;

  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "Rpc %u, lsn %u: Trying to reset server session. State = %s. Issue",
          rpc_id_, session->local_session_num_,
          session_state_str(session->state_).c_str());

  size_t pending_enqueue_resps = 0;
  for (const SSlot &sslot : session->sslot_arr_) {
    if (sslot.server_info_.req_type_ != kInvalidReqType) {
      pending_enqueue_resps++;
    }
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
        rpc_id_, session->local_session_num_, pending_enqueue_resps);

    return false;
  }
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

/**
 * @file rpc_disconnect_handlers.cc
 * @brief Handlers for session management disconnect requests and responses.
 */
#include "rpc.h"

namespace erpc {

// We don't need to check remote arguments since the session was already
// connected successfully.
template <class TTr>
void Rpc<TTr>::handle_disconnect_req_st(const SmPkt &sm_pkt) {
  assert(in_dispatch());
  assert(sm_pkt.pkt_type_ == SmPktType::kDisconnectReq &&
         sm_pkt.server_.rpc_id_ == rpc_id_);

  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg, "Rpc %u: Received disconnect request from %s. Issue",
          rpc_id_, sm_pkt.client_.name().c_str());

  uint16_t session_num = sm_pkt.server_.session_num_;
  assert(session_num < session_vec_.size());

  // Handle reordering. We don't need the session token for this.
  Session *session = session_vec_.at(session_num);
  if (session == nullptr) {
    ERPC_INFO("%s: Duplicate request. Re-sending response.\n", issue_msg);
    sm_pkt_udp_tx_st(sm_construct_resp(sm_pkt, SmErrType::kNoError));
    return;
  }

  // If we're here, this is the first time we're receiving disconnect request
  assert(session->is_server() && session->is_connected());
  assert(session->server_ == sm_pkt.server_);
  assert(session->client_ == sm_pkt.client_);

  // Responses for all sslots must have been sent
  for (const SSlot &sslot : session->sslot_arr_) {
    const auto &si = sslot.server_info_;
    _unused(si);

    assert(si.req_msgbuf_.is_buried() && si.req_type_ == kInvalidReqType);

    // If there's a response in this sslot, we've finished sending it
    if (sslot.tx_msgbuf_ != nullptr) {
      assert(si.num_rx_ ==
             si.sav_num_req_pkts_ + sslot.tx_msgbuf_->num_pkts_ - 1);
    }
  }

  free_ring_entries();

  ERPC_INFO("%s. None. Sending response.\n", issue_msg);
  sm_pkt_udp_tx_st(sm_construct_resp(sm_pkt, SmErrType::kNoError));

  bury_session_st(session);
}

// We free the session's ring bufs before sending the disconnect request, so
// not here.
template <class TTr>
void Rpc<TTr>::handle_disconnect_resp_st(const SmPkt &sm_pkt) {
  assert(in_dispatch());
  assert(sm_pkt.pkt_type_ == SmPktType::kDisconnectResp &&
         sm_pkt.client_.rpc_id_ == rpc_id_);
  assert(sm_pkt.err_type_ == SmErrType::kNoError);  // Disconnects don't fail

  // Create the basic issue message using only the packet
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "Rpc %u: Received disconnect response from %s for session %u. Issue",
          rpc_id_, sm_pkt.server_.name().c_str(), sm_pkt.client_.session_num_);

  uint16_t session_num = sm_pkt.client_.session_num_;
  assert(session_num < session_vec_.size());

  // Handle reordering. We don't need the session token for this.
  Session *session = session_vec_[session_num];
  if (session == nullptr) {
    ERPC_INFO("%s: Duplicate response. Ignoring.\n", issue_msg);
    return;
  }

  assert(session->is_client());
  assert(session->state_ == SessionState::kDisconnectInProgress);
  assert(session->client_ == sm_pkt.client_);
  assert(session->server_ == sm_pkt.server_);

  ERPC_INFO("%s: None. Session disconnected.\n", issue_msg);
  free_ring_entries();  // Free before callback to allow creating a new session
  sm_handler_(session->local_session_num_, SmEventType::kDisconnected,
              SmErrType::kNoError, context_);
  bury_session_st(session);
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

/**
 * @file rpc_connect_handlers.cc
 * @brief Handlers for session management connect requests and responses.
 */
#include "rpc.h"

namespace erpc {

// We need to handle all types of errors in remote arguments that the client can
// make when calling create_session(), which cannot check for such errors.
template <class TTr>
void Rpc<TTr>::handle_connect_req_st(const SmPkt &sm_pkt) {
  assert(in_dispatch());
  assert(sm_pkt.pkt_type_ == SmPktType::kConnectReq &&
         sm_pkt.server_.rpc_id_ == rpc_id_);

  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg, "Rpc %u: Received connect request from %s. Issue", rpc_id_,
          sm_pkt.client_.name().c_str());

  // Handle duplicate session connect requests
  if (conn_req_token_map_.count(sm_pkt.uniq_token_) > 0) {
    uint16_t srv_session_num = conn_req_token_map_[sm_pkt.uniq_token_];
    assert(session_vec_.size() > srv_session_num);

    const Session *session = session_vec_[srv_session_num];
    if (session == nullptr || session->state_ != SessionState::kConnected) {
      ERPC_INFO("%s: Duplicate request, and response is unneeded.\n",
                issue_msg);
      return;
    } else {
      SmPkt resp_sm_pkt = sm_construct_resp(sm_pkt, SmErrType::kNoError);
      resp_sm_pkt.server_ = session->server_;  // Re-send server endpoint info

      ERPC_INFO("%s: Duplicate request. Re-sending response.\n", issue_msg);
      sm_pkt_udp_tx_st(resp_sm_pkt);
      return;
    }
  }

  // If we're here, this is the first time we're receiving this connect request

  // Check that the transport matches
  if (sm_pkt.server_.transport_type_ != transport_->transport_type_) {
    ERPC_WARN("%s: Invalid transport %s. Sending response.\n", issue_msg,
              Transport::get_name(sm_pkt.server_.transport_type_).c_str());
    sm_pkt_udp_tx_st(sm_construct_resp(sm_pkt, SmErrType::kInvalidTransport));
    return;
  }

  // Check if we are allowed to create another session
  if (!have_ring_entries()) {
    ERPC_WARN("%s: Ring buffers exhausted. Sending response.\n", issue_msg);
    sm_pkt_udp_tx_st(sm_construct_resp(sm_pkt, SmErrType::kRingExhausted));
    return;
  }

  // Try to resolve the client-provided routing info. If session creation
  // succeeds, we'll copy it to the server's session endpoint.
  Transport::routing_info_t client_rinfo = sm_pkt.client_.routing_info_;
  bool resolve_success;
  if (kTesting && faults_.fail_resolve_rinfo_) {
    resolve_success = false;
  } else {
    resolve_success = transport_->resolve_remote_routing_info(&client_rinfo);
  }

  if (!resolve_success) {
    std::string routing_info_str = TTr::routing_info_str(&client_rinfo);
    ERPC_WARN("%s: Unable to resolve routing info %s. Sending response.\n",
              issue_msg, routing_info_str.c_str());
    sm_pkt_udp_tx_st(
        sm_construct_resp(sm_pkt, SmErrType::kRoutingResolutionFailure));
    return;
  }

  // If we are here, create a new session and fill preallocated MsgBuffers
  auto *session = new Session(Session::Role::kServer, sm_pkt.uniq_token_,
                              get_freq_ghz(), transport_->get_bandwidth());
  session->state_ = SessionState::kConnected;

  for (size_t i = 0; i < kSessionReqWindow; i++) {
    MsgBuffer &msgbuf_i = session->sslot_arr_[i].pre_resp_msgbuf_;
    msgbuf_i = alloc_msg_buffer(pre_resp_msgbuf_size_);

    if (msgbuf_i.buf_ == nullptr) {
      // Cleanup everything allocated for this session
      for (size_t j = 0; j < i; j++) {
        MsgBuffer &msgbuf_j = session->sslot_arr_[j].pre_resp_msgbuf_;
        assert(msgbuf_j.buf_ != nullptr);
        free_msg_buffer(msgbuf_j);
      }

      free(session);
      ERPC_WARN("%s: Failed to allocate prealloc MsgBuffer.\n", issue_msg);
      sm_pkt_udp_tx_st(sm_construct_resp(sm_pkt, SmErrType::kOutOfMemory));
      return;
    }
  }

  // Fill-in the server endpoint
  session->server_ = sm_pkt.server_;
  session->server_.session_num_ = session_vec_.size();
  transport_->fill_local_routing_info(&session->server_.routing_info_);
  conn_req_token_map_[session->uniq_token_] = session->server_.session_num_;

  // Fill-in the client endpoint
  session->client_ = sm_pkt.client_;
  session->client_.routing_info_ = client_rinfo;

  session->local_session_num_ = session->server_.session_num_;
  session->remote_session_num_ = session->client_.session_num_;

  alloc_ring_entries();
  session_vec_.push_back(session);  // Add to list of all sessions

  // Add server endpoint info created above to resp. No need to add client info.
  SmPkt resp_sm_pkt = sm_construct_resp(sm_pkt, SmErrType::kNoError);
  resp_sm_pkt.server_ = session->server_;

  ERPC_INFO("%s: None. Sending response.\n", issue_msg);
  sm_pkt_udp_tx_st(resp_sm_pkt);
  return;
}

template <class TTr>
void Rpc<TTr>::handle_connect_resp_st(const SmPkt &sm_pkt) {
  assert(in_dispatch());
  assert(sm_pkt.pkt_type_ == SmPktType::kConnectResp &&
         sm_pkt.client_.rpc_id_ == rpc_id_);

  // Create the basic issue message using only the packet
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "Rpc %u: Received connect response from %s for session %u. Issue",
          rpc_id_, sm_pkt.server_.name().c_str(), sm_pkt.client_.session_num_);

  uint16_t session_num = sm_pkt.client_.session_num_;
  assert(session_num < session_vec_.size());

  // Handle reordering. We don't need the session token for this.
  Session *session = session_vec_[session_num];
  if (session == nullptr ||
      session->state_ != SessionState::kConnectInProgress) {
    ERPC_INFO("%s: Duplicate response. Ignoring.\n", issue_msg);
    return;
  }

  assert(session->is_client() && session->client_ == sm_pkt.client_);

  // We don't have the server's session number locally yet, so we cannot use
  // SessionEndpoint comparator to compare server endpoint metadata.
  assert(strcmp(session->server_.hostname_, sm_pkt.server_.hostname_) == 0);
  assert(session->server_.rpc_id_ == sm_pkt.server_.rpc_id_);
  assert(session->server_.session_num_ == kInvalidSessionNum);

  // Handle special error cases for which we retry the connect request
  if (sm_pkt.err_type_ == SmErrType::kInvalidRemoteRpcId) {
    if (retry_connect_on_invalid_rpc_id_) {
      ERPC_INFO("%s: Invalid remote Rpc ID. Dropping. Scan will retry later.\n",
                issue_msg);
      sm_pending_reqs_.insert(session->local_session_num_);  // Duplicates fine
      return;
    }
  }

  if (sm_pkt.err_type_ != SmErrType::kNoError) {
    // The server didn't allocate session resources, so we can just destroy
    ERPC_WARN("%s: Error %s.\n", issue_msg,
              sm_err_type_str(sm_pkt.err_type_).c_str());

    free_ring_entries();  // Free before callback to allow creating new session
    sm_handler_(session->local_session_num_, SmEventType::kConnectFailed,
                sm_pkt.err_type_, context_);
    bury_session_st(session);

    return;
  }

  // If we are here, the server has created a session endpoint

  // Try to resolve the server-provided routing info
  Transport::routing_info_t srv_routing_info = sm_pkt.server_.routing_info_;
  bool resolve_success;
  if (kTesting && faults_.fail_resolve_rinfo_) {
    resolve_success = false;  // Inject fault
  } else {
    resolve_success =
        transport_->resolve_remote_routing_info(&srv_routing_info);
  }

  if (!resolve_success) {
    // Free server resources by disconnecting. No connected (with error)
    // callback will be invoked.
    ERPC_WARN("%s: Failed to resolve server routing info. Disconnecting.\n",
              issue_msg);

    session->server_ = sm_pkt.server_;  // Needed for disconnect response later

    // Do what destroy_session() does with a connected session
    session->state_ = SessionState::kDisconnectInProgress;
    send_sm_req_st(session);
    return;
  }

  // Save server endpoint metadata
  session->server_ = sm_pkt.server_;  // This fills most fields
  session->server_.routing_info_ = srv_routing_info;
  session->remote_session_num_ = session->server_.session_num_;
  session->state_ = SessionState::kConnected;

  session->client_info_.cc_.prev_desired_tx_tsc_ = rdtsc();

  ERPC_INFO("%s: None. Session connected.\n", issue_msg);
  sm_handler_(session->local_session_num_, SmEventType::kConnected,
              SmErrType::kNoError, context_);
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

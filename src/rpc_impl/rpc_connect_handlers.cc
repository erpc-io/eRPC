/**
 * @file rpc_connect_handlers.cc
 * @brief Handlers for session management connect requests and responses.
 */
#include "rpc.h"

namespace erpc {

// We need to handle all types of errors in remote arguments that the client can
// make when calling create_session(), which cannot check for such errors.
template <class TTr>
void Rpc<TTr>::handle_connect_req_st(const SmWorkItem &req_wi) {
  assert(in_dispatch());
  assert(req_wi.sm_pkt.pkt_type == SmPktType::kConnectReq);

  // Ensure that server fields known by the client were filled correctly
  assert(strcmp(req_wi.sm_pkt.server.hostname, nexus->hostname.c_str()) == 0);
  assert(req_wi.sm_pkt.server.rpc_id == rpc_id);

  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg, "eRPC Rpc %u: Received connect request from %s. Issue",
          rpc_id, req_wi.sm_pkt.client.name().c_str());

  // Check that the transport matches
  Transport::TransportType pkt_tr_type = req_wi.sm_pkt.server.transport_type;
  if (pkt_tr_type != transport->transport_type) {
    LOG_WARN("%s: Invalid transport %s. Sending response.\n", issue_msg,
             Transport::get_transport_name(pkt_tr_type).c_str());
    enqueue_sm_resp_st(req_wi, SmErrType::kInvalidTransport);
    return;
  }

  // Check if the requested physical port is correct
  if (req_wi.sm_pkt.server.phy_port != phy_port) {
    LOG_WARN("%s: Invalid server port %u. Sending response.\n", issue_msg,
             req_wi.sm_pkt.server.phy_port);
    enqueue_sm_resp_st(req_wi, SmErrType::kInvalidRemotePort);
    return;
  }

  // Check if we are allowed to create another session
  if (!have_recvs()) {
    LOG_WARN("%s: RECVs exhausted. Sending response.\n", issue_msg);
    enqueue_sm_resp_st(req_wi, SmErrType::kRecvsExhausted);
  }

  if (session_vec.size() == kMaxSessionsPerThread) {
    LOG_WARN("%s: Reached session limit %zu. Sending response.\n", issue_msg,
             kMaxSessionsPerThread);
    enqueue_sm_resp_st(req_wi, SmErrType::kTooManySessions);
    return;
  }

  // Try to resolve the client-provided routing info. If session creation
  // succeeds, we'll copy it to the server's session endpoint.
  Transport::RoutingInfo client_rinfo = req_wi.sm_pkt.client.routing_info;
  bool resolve_success = transport->resolve_remote_routing_info(&client_rinfo);
  if (!resolve_success) {
    std::string routing_info_str = TTr::routing_info_str(&client_rinfo);
    LOG_WARN("%s: Unable to resolve routing info %s. Sending response.\n",
             issue_msg, routing_info_str.c_str());
    enqueue_sm_resp_st(req_wi, SmErrType::kRoutingResolutionFailure);
    return;
  }

  // If we are here, create a new session and fill preallocated MsgBuffers
  auto *session = new Session(Session::Role::kServer);
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    MsgBuffer &msgbuf_i = session->sslot_arr[i].pre_resp_msgbuf;
    msgbuf_i = alloc_msg_buffer(TTr::kMaxDataPerPkt);

    if (msgbuf_i.buf == nullptr) {
      // Cleanup everything allocated for this session
      for (size_t j = 0; j < i; j++) {
        MsgBuffer &msgbuf_j = session->sslot_arr[j].pre_resp_msgbuf;
        assert(msgbuf_j.buf != nullptr);
        free_msg_buffer(msgbuf_j);
      }

      LOG_WARN("%s: Failed to allocate prealloc MsgBuffer.\n", issue_msg);
      enqueue_sm_resp_st(req_wi, SmErrType::kOutOfMemory);
      return;
    }
  }

  // Record info to session
  session->server = req_wi.sm_pkt.server;
  session->server.session_num = session_vec.size();
  transport->fill_local_routing_info(&session->server.routing_info);

  session->client = req_wi.sm_pkt.client;
  session->client.routing_info = client_rinfo;

  session->local_session_num = session->server.session_num;
  session->remote_session_num = session->client.session_num;

  alloc_recvs();
  session_vec.push_back(session);  // Add to list of all sessions

  // For successful responses, we need to edit the response SM packet
  SmWorkItem resp_wi = req_wi;
  resp_wi.sm_pkt.server = session->server;
  resp_wi.sm_pkt.client = session->client;

  LOG_INFO("%s: None. Sending response.\n", issue_msg);
  enqueue_sm_resp_st(resp_wi, SmErrType::kNoError);
  return;
}

template <class TTr>
void Rpc<TTr>::handle_connect_resp_st(const SmPkt &sm_pkt) {
  assert(in_dispatch());
  assert(sm_pkt.pkt_type == SmPktType::kConnectResp);
  assert(sm_err_type_is_valid(sm_pkt.err_type));

  // Create the basic issue message using only the packet
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %u: Received connect response from %s for session %u. "
          "Issue",
          rpc_id, sm_pkt.server.name().c_str(), sm_pkt.client.session_num);

  // Try to locate the requester session and do some sanity checks
  uint16_t session_num = sm_pkt.client.session_num;
  assert(session_num < session_vec.size());

  Session *session = session_vec[session_num];
  assert(session != nullptr);
  assert(session->is_client());
  assert(session->state == SessionState::kConnectInProgress);
  assert(session->client == sm_pkt.client);

  // We don't have the server's session number locally yet, so we cannot use
  // SessionEndpoint comparator to compare server endpoint metadata.
  assert(strcmp(session->server.hostname, sm_pkt.server.hostname) == 0);
  assert(session->server.rpc_id == sm_pkt.server.rpc_id);
  assert(session->server.session_num == kInvalidSessionNum);

  // Handle special error cases for which we retry the connect request
  if (sm_pkt.err_type == SmErrType::kInvalidRemoteRpcId) {
    if (retry_connect_on_invalid_rpc_id) {
      LOG_WARN("%s: Invalid remote Rpc ID. Retrying.\n", issue_msg);
      enqueue_sm_req_st(session, SmPktType::kConnectReq);
      return;
    }
  }

  if (sm_pkt.err_type != SmErrType::kNoError) {
    // The server didn't allocate session resources, so we can just destroy
    LOG_WARN("%s: Error %s.\n", issue_msg,
             sm_err_type_str(sm_pkt.err_type).c_str());

    free_recvs();  // Free before SM callback to allow creating a new session
    sm_handler(session->local_session_num, SmEventType::kConnectFailed,
               sm_pkt.err_type, context);
    bury_session_st(session);

    return;
  }

  // If we are here, the server has created a session endpoint

  // Try to resolve the server-provided routing info
  Transport::RoutingInfo srv_routing_info = sm_pkt.server.routing_info;
  bool resolve_success;
  if (kFaultInjection && faults.fail_resolve_server_rinfo) {
    resolve_success = false;  // Inject fault
  } else {
    resolve_success = transport->resolve_remote_routing_info(&srv_routing_info);
  }

  if (!resolve_success) {
    // Free server resources by disconnecting. No connected (with error)
    // callback will be invoked.
    LOG_WARN("%s: Failed to resolve server routing info. Disconnecting.\n",
             issue_msg);

    session->server = sm_pkt.server;  // Needed for disconnect response later

    // Do what destroy_session() does with a connected session
    session->state = SessionState::kDisconnectInProgress;
    enqueue_sm_req_st(session, SmPktType::kDisconnectReq);
    return;
  }

  // Save server endpoint metadata
  session->server = sm_pkt.server;  // This fills most fields
  session->server.routing_info = srv_routing_info;
  session->remote_session_num = session->server.session_num;
  session->state = SessionState::kConnected;

  LOG_INFO("%s: None. Session connected.\n", issue_msg);
  sm_handler(session->local_session_num, SmEventType::kConnected,
             SmErrType::kNoError, context);
}

}  // End erpc

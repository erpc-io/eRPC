/**
 * @file rpc_sm_api.cc
 * @brief Rpc session functions that are exposed to the user.
 */
#include <algorithm>

#include "rpc.h"

namespace erpc {

// This function is not on the critical path and is exposed to the user,
// so the args checking is always enabled.
template <class TTr>
int Rpc<TTr>::create_session_st(std::string rem_hostname, uint8_t rem_rpc_id,
                                uint8_t rem_phy_port) {
  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg, "eRPC Rpc %u: create_session() failed. Issue", rpc_id);

  // Check that the caller is the creator thread
  if (!in_dispatch()) {
    LOG_WARN("%s: Caller thread is not the creator thread.\n", issue_msg);
    return -EPERM;
  }

  // Check remote fabric port
  if (rem_phy_port >= kMaxPhyPorts) {
    LOG_WARN("%s: Invalid remote fabric port %u.\n", issue_msg, rem_phy_port);
    return -EINVAL;
  }

  // Check remote hostname
  if (rem_hostname.length() == 0 || rem_hostname.length() > kMaxHostnameLen) {
    LOG_WARN("%s: Invalid remote hostname.\n", issue_msg);
    return -EINVAL;
  }

  // Creating a session to one's own Rpc as the client is not allowed
  if (rem_hostname == nexus->hostname && rem_rpc_id == rpc_id) {
    LOG_WARN("%s: Remote Rpc is same as local.\n", issue_msg);
    return -EINVAL;
  }

  // Ensure bounded session_vec size
  if (session_vec.size() >= kMaxSessionsPerThread) {
    LOG_WARN("%s: Session limit (%zu) reached.\n", issue_msg,
             kMaxSessionsPerThread);
    return -ENOMEM;
  }

  // Ensure that we have RECV credits for this session
  if (!have_recvs()) {
    LOG_WARN("%s: RECVs exhausted.\n", issue_msg);
    return -ENOMEM;
  }

  auto *session = new Session(Session::Role::kClient);

  // Fill prealloc response MsgBuffers for the client session
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    MsgBuffer &resp_msgbuf_i = session->sslot_arr[i].pre_resp_msgbuf;
    resp_msgbuf_i = alloc_msg_buffer(TTr::kMaxDataPerPkt);

    if (resp_msgbuf_i.buf == nullptr) {
      // Cleanup everything allocated for this session
      for (size_t j = 0; j < i; j++) {
        MsgBuffer &resp_msgbuf_j = session->sslot_arr[j].pre_resp_msgbuf;
        assert(resp_msgbuf_j.buf != nullptr);
        free_msg_buffer(resp_msgbuf_j);
      }

      LOG_WARN("%s: Failed to allocate prealloc MsgBuffer.\n", issue_msg);
      return -ENOMEM;
    }
  }

  session->local_session_num = session_vec.size();

  // Fill in client and server endpoint metadata. Commented server fields will
  // be filled when the connect response is received.
  SessionEndpoint &client_endpoint = session->client;
  client_endpoint.transport_type = transport->transport_type;
  strcpy(client_endpoint.hostname, nexus->hostname.c_str());
  client_endpoint.phy_port = phy_port;
  client_endpoint.rpc_id = rpc_id;
  client_endpoint.session_num = session->local_session_num;
  transport->fill_local_routing_info(&client_endpoint.routing_info);

  SessionEndpoint &server_endpoint = session->server;
  server_endpoint.transport_type = transport->transport_type;
  strcpy(server_endpoint.hostname, rem_hostname.c_str());
  server_endpoint.phy_port = rem_phy_port;
  server_endpoint.rpc_id = rem_rpc_id;
  // server_endpoint.session_num = ??
  // server_endpoint.routing_info = ??

  alloc_recvs();
  session_vec.push_back(session);  // Add to list of all sessions
  enqueue_sm_req_st(session, SmPktType::kConnectReq);

  return client_endpoint.session_num;
}

template <class TTr>
int Rpc<TTr>::destroy_session_st(int session_num) {
  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg,
          "eRPC Rpc %u: destroy_session() failed for session %d. Issue", rpc_id,
          session_num);

  if (!in_dispatch()) {
    LOG_WARN("%s: Caller thread is not creator.\n", issue_msg);
    return -EPERM;
  }

  if (!is_usr_session_num_in_range_st(session_num)) {
    LOG_WARN("%s: Invalid session number.\n", issue_msg);
    return -EINVAL;
  }

  Session *session = session_vec[static_cast<size_t>(session_num)];
  if (session == nullptr) {
    LOG_WARN("%s: Session already destroyed.\n", issue_msg);
    return -EPERM;
  }

  if (!session->is_client()) {
    LOG_WARN("%s: User cannot destroy server session.\n", issue_msg);
    return -EINVAL;
  }

  // A session can be destroyed only when all its sslots are free
  if (session->client_info.sslot_free_vec.size() !=
      Session::kSessionReqWindow) {
    LOG_WARN("%s: Session has pending RPC requests.\n", issue_msg);
    return -EBUSY;
  }

  // If we're here, RX and TX MsgBuffers in all sslots should be already buried
  for (const SSlot &sslot : session->sslot_arr) {
    assert(sslot.tx_msgbuf == nullptr);
    if (!sslot.is_client) assert(sslot.server_info.req_msgbuf.buf == nullptr);
  }

  switch (session->state) {
    case SessionState::kConnectInProgress:
      // Can't disconnect right now. User needs to wait.
      LOG_WARN("%s: Session connection in progress.\n", issue_msg);
      return -EPERM;

    case SessionState::kConnected:
      LOG_INFO(
          "eRPC Rpc %u: Sending disconnect request for session %u "
          "to [%s, %u].\n",
          rpc_id, session->local_session_num, session->server.hostname,
          session->server.rpc_id);

      session->state = SessionState::kDisconnectInProgress;
      enqueue_sm_req_st(session, SmPktType::kDisconnectReq);
      return 0;

    case SessionState::kDisconnectInProgress:
      LOG_WARN("%s: Session disconnection in progress.\n", issue_msg);
      return -EALREADY;

    case SessionState::kResetInProgress:
      LOG_WARN("%s: None. Session reset in progress.\n", issue_msg);
      return 0;

    default:
      throw std::runtime_error("Invalid session state");
  }
}

template <class TTr>
size_t Rpc<TTr>::num_active_sessions_st() {
  assert(in_dispatch());

  size_t ret = 0;
  // session_vec can only be modified by the creator, so no need to lock
  for (Session *session : session_vec) {
    if (session != nullptr) ret++;
  }

  return ret;
}

}  // End erpc

/**
 * @file rpc_sm_api.cc
 * @brief Rpc session functions that are exposed to the user.
 */
#include <algorithm>
#include "util/autorun_helpers.h"

#include "rpc.h"

namespace erpc {

// This function is not on the critical path and is exposed to the user,
// so the args checking is always enabled.
template <class TTr>
int Rpc<TTr>::create_session_st(std::string remote_uri, uint8_t rem_rpc_id) {
  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg, "Rpc %u: create_session() failed. Issue", rpc_id_);

  // Check that the caller is the creator thread
  if (!in_dispatch()) {
    ERPC_WARN(
        "%s: Cannot create session from a thread other than the one that "
        "created this Rpc object.\n",
        issue_msg);
    return -EPERM;
  }

  std::string rem_hostname = extract_hostname_from_uri(remote_uri);
  uint16_t rem_sm_udp_port = extract_udp_port_from_uri(remote_uri);

  // Check remote hostname
  if (rem_hostname.length() == 0 || rem_hostname.length() > kMaxHostnameLen) {
    ERPC_WARN("%s: Invalid remote hostname.\n", issue_msg);
    return -EINVAL;
  }

  // Creating a session to one's own Rpc as the client is not allowed
  if (rem_hostname == nexus_->hostname_ && rem_rpc_id == rpc_id_ &&
      rem_sm_udp_port == nexus_->sm_udp_port_) {
    ERPC_WARN("%s: Remote Rpc is same as local.\n", issue_msg);
    return -EINVAL;
  }

  // Ensure that we have ring buffers for this session
  if (!have_ring_entries()) {
    ERPC_WARN("%s: Ring buffers exhausted.\n", issue_msg);
    return -ENOMEM;
  }

  auto *session = new Session(Session::Role::kClient, slow_rand_.next_u64(),
                              get_freq_ghz(), transport_->get_bandwidth());
  session->state_ = SessionState::kConnectInProgress;
  session->local_session_num_ = session_vec_.size();

  // Fill in client and server endpoint metadata. Commented server fields will
  // be filled when the connect response is received.
  SessionEndpoint &client_endpoint = session->client_;
  client_endpoint.transport_type_ = transport_->transport_type_;
  strcpy(client_endpoint.hostname_, nexus_->hostname_.c_str());
  client_endpoint.sm_udp_port_ = nexus_->sm_udp_port_;
  client_endpoint.rpc_id_ = rpc_id_;
  client_endpoint.session_num_ = session->local_session_num_;
  transport_->fill_local_routing_info(&client_endpoint.routing_info_);

  SessionEndpoint &server_endpoint = session->server_;
  server_endpoint.transport_type_ = transport_->transport_type_;
  strcpy(server_endpoint.hostname_, rem_hostname.c_str());
  server_endpoint.sm_udp_port_ = rem_sm_udp_port;
  server_endpoint.rpc_id_ = rem_rpc_id;
  // server_endpoint.session_num = ??
  // server_endpoint.routing_info = ??

  alloc_ring_entries();
  session_vec_.push_back(session);  // Add to list of all sessions

  send_sm_req_st(session);
  return client_endpoint.session_num_;
}

template <class TTr>
int Rpc<TTr>::destroy_session_st(int session_num) {
  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg, "Rpc %u, lsn %u: destroy_session() failed. Issue", rpc_id_,
          session_num);

  if (!in_dispatch()) {
    ERPC_WARN(
        "%s: Can't destroy session from a thread other than the one that "
        "created this Rpc object.\n",
        issue_msg);
    return -EPERM;
  }

  if (!is_usr_session_num_in_range_st(session_num)) {
    ERPC_WARN("%s: Invalid session number.\n", issue_msg);
    return -EINVAL;
  }

  Session *session = session_vec_[static_cast<size_t>(session_num)];
  if (session == nullptr) {
    ERPC_WARN("%s: Session already destroyed.\n", issue_msg);
    return -EPERM;
  }

  if (!session->is_client()) {
    ERPC_WARN("%s: User cannot destroy server session.\n", issue_msg);
    return -EINVAL;
  }

  // A session can be destroyed only when all its sslots are free
  if (session->client_info_.sslot_free_vec_.size() != kSessionReqWindow) {
    ERPC_WARN("%s: Session has pending RPC requests.\n", issue_msg);
    return -EBUSY;
  }

  // If we're here, RX and TX MsgBuffers in all sslots should be already buried
  for (const SSlot &sslot : session->sslot_arr_) {
    assert(sslot.tx_msgbuf_ == nullptr);
    if (!sslot.is_client_)
      assert(sslot.server_info_.req_msgbuf_.buf_ == nullptr);
  }

  switch (session->state_) {
    case SessionState::kConnectInProgress:
      // Can't disconnect right now. User needs to wait.
      ERPC_WARN("%s: Session connection in progress.\n", issue_msg);
      return -EPERM;

    case SessionState::kConnected:
      ERPC_INFO("Rpc %u, lsn %u: Sending disconnect request to [%s, %u].\n",
                rpc_id_, session->local_session_num_,
                session->server_.hostname_, session->server_.rpc_id_);

      session->state_ = SessionState::kDisconnectInProgress;
      send_sm_req_st(session);
      return 0;

    case SessionState::kDisconnectInProgress:
      ERPC_WARN("%s: Session disconnection in progress.\n", issue_msg);
      return -EALREADY;

    case SessionState::kResetInProgress:
      ERPC_WARN("%s: None. Session reset in progress.\n", issue_msg);
      return 0;

    default: throw std::runtime_error("Invalid session state");
  }
}

template <class TTr>
size_t Rpc<TTr>::num_active_sessions_st() {
  assert(in_dispatch());

  size_t ret = 0;
  // session_vec can only be modified by the creator, so no need to lock
  for (Session *session : session_vec_) {
    if (session != nullptr) ret++;
  }

  return ret;
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

/**
 * @file rpc_connect_handlers.cc
 * @brief Handlers for session management connect requests and responses.
 */
#include "rpc.h"
#include <algorithm>

namespace ERpc {

/*
 * We need to handle all types of errors in remote arguments that the client can
 * make when calling create_session(), which cannot check for such errors.
 */
template <class TTr>
void Rpc<TTr>::handle_session_connect_req(SessionMgmtPkt *sm_pkt) {
  assert(in_creator());
  assert(sm_pkt != NULL);
  assert(sm_pkt->pkt_type == SessionMgmtPktType::kConnectReq);

  /* Ensure that server fields known by the client were filled correctly */
  assert(strcmp(sm_pkt->server.hostname, nexus->hostname.c_str()) == 0);
  assert(sm_pkt->server.app_tid == app_tid);
  assert(sm_pkt->server.secret == sm_pkt->client.secret);

  /* Create the basic issue message */
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg, "eRPC Rpc %u: Received connect request from %s. Issue",
          app_tid, sm_pkt->client.name().c_str());

  /* Check that the transport matches */
  if (sm_pkt->server.transport_type != transport->transport_type) {
    erpc_dprintf("%s: Invalid transport %s. Sending response.\n", issue_msg,
                 get_transport_name(sm_pkt->server.transport_type).c_str());

    sm_pkt->send_resp_mut(SessionMgmtErrType::kInvalidTransport,
                          &nexus->udp_config);
    return;
  }

  /* Check if the requested physical port is correct */
  if (sm_pkt->server.phy_port != phy_port) {
    erpc_dprintf("%s: Invalid server port %u. Sending response.\n", issue_msg,
                 sm_pkt->server.phy_port);

    sm_pkt->send_resp_mut(SessionMgmtErrType::kInvalidRemotePort,
                          &nexus->udp_config);
    return;
  }

  /*
   * Check if we (= this Rpc) already have a session as the server with the
   * client Rpc (C) that sent this packet. (This is different from if we have a
   * session as the client Rpc, where C is the server Rpc.) This happens when
   * the connect request is retransmitted.
   */
  for (Session *old_session : session_vec) {
    /*
     * This check ensures that we own the session as the server.
     *
     * If the check succeeds, we cannot own old_session as the client:
     * sm_pkt was sent by a different Rpc than us, since an Rpc cannot send
     * session management packets to itself. So the client hostname and app_tid
     * in the located session cannot be ours, since they are same as sm_pkt's.
     */
    if ((old_session != nullptr) &&
        strcmp(old_session->client.hostname, sm_pkt->client.hostname) == 0 &&
        (old_session->client.app_tid == sm_pkt->client.app_tid)) {
      assert(old_session->is_server());
      assert(old_session->is_connected());

      /* There's a valid session, so client endpoint metadata is unchanged */
      assert(old_session->client == sm_pkt->client);

      erpc_dprintf("%s: Duplicate session connect request. Sending response.\n",
                   issue_msg);

      /* Send a connect success response */
      sm_pkt->server = old_session->server; /* Fill server endpoint metadata */
      sm_pkt->send_resp_mut(SessionMgmtErrType::kNoError, &nexus->udp_config);
      return;
    }
  }

  /* Check if we are allowed to create another session */
  if (session_vec.size() == kMaxSessionsPerThread) {
    erpc_dprintf("%s: Reached session limit %zu. Sending response.\n",
                 issue_msg, kMaxSessionsPerThread);

    sm_pkt->send_resp_mut(SessionMgmtErrType::kTooManySessions,
                          &nexus->udp_config);
    return;
  }

  /*
   * Try to resolve the client's routing info into the packet. If session
   * creation succeeds, we'll copy it to the server's session endpoint.
   */
  RoutingInfo *client_rinfo = &(sm_pkt->client.routing_info);
  erpc_dprintf("eRPC Rpc %u: Resolving client's routing info %s.\n", app_tid,
               TTr::routing_info_str(client_rinfo).c_str());

  bool resolve_success = transport->resolve_remote_routing_info(client_rinfo);
  if (!resolve_success) {
    erpc_dprintf("%s: Unable to resolve routing info %s. Sending response.\n",
                 issue_msg, TTr::routing_info_str(client_rinfo).c_str());
    sm_pkt->send_resp_mut(SessionMgmtErrType::kRoutingResolutionFailure,
                          &nexus->udp_config);
    return;
  }

  /*
   * If we are here, create a new session and fill preallocated MsgBuffers.
   * XXX: Use pool?
   */
  Session *session =
      new Session(Session::Role::kServer, SessionState::kConnected);
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    MsgBuffer &msgbuf_i = session->sslot_arr[i].pre_resp_msgbuf;
    msgbuf_i = alloc_msg_buffer(TTr::kMaxDataPerPkt);

    if (msgbuf_i.buf == nullptr) {
      /*
       * We haven't assigned a session number or allocated non-prealloc
       * MsgBuffers yet, so just free prealloc MsgBuffers 0 -- (i - 1).
       */
      for (size_t j = 0; j < i; j++) {
        MsgBuffer &msgbuf_j = session->sslot_arr[j].pre_resp_msgbuf;
        assert(msgbuf_j.buf != nullptr);
        free_msg_buffer(msgbuf_j);
      }

      erpc_dprintf("%s: Failed to allocate prealloc MsgBuffer.\n", issue_msg);
      sm_pkt->send_resp_mut(SessionMgmtErrType::kOutOfMemory,
                            &nexus->udp_config);
      return;
    }
  }

  /*
   * Set the server endpoint metadata fields in the received packet, which we
   * will then send back to the client.
   */
  sm_pkt->server.session_num = session_vec.size();
  transport->fill_local_routing_info(&(sm_pkt->server.routing_info));

  // TEMP
  fprintf(stderr, "Server routing info = %s\n",
          TTr::routing_info_str(&(sm_pkt->server.routing_info)).c_str());

  /* Save endpoint metadata from pkt. This saves the resolved routing info. */
  session->server = sm_pkt->server;
  session->client = sm_pkt->client;

  session->local_session_num = sm_pkt->server.session_num;
  session->remote_session_num = sm_pkt->client.session_num;

  session_vec.push_back(session); /* Add to list of all sessions */

  erpc_dprintf("%s: None. Sending response.\n", issue_msg);
  sm_pkt->send_resp_mut(SessionMgmtErrType::kNoError, &nexus->udp_config);
  return;
}

template <class TTr>
void Rpc<TTr>::handle_session_connect_resp(SessionMgmtPkt *sm_pkt) {
  assert(in_creator());
  assert(sm_pkt != NULL);
  assert(sm_pkt->pkt_type == SessionMgmtPktType::kConnectResp);
  assert(session_mgmt_err_type_is_valid(sm_pkt->err_type));

  /* Create the basic issue message using only the packet */
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %u: Received connect response from %s for session %u. "
          "Issue",
          app_tid, sm_pkt->server.name().c_str(), sm_pkt->client.session_num);

  /* Try to locate the requester session for this response */
  uint16_t session_num = sm_pkt->client.session_num;
  assert(session_num < session_vec.size());

  Session *session = session_vec[session_num];

  /*
   * Check if the client session was already disconnected. This happens when
   * we get a (massively delayed) out-of-order duplicate connect response after
   * a disconnect response. If so, the callback is not invoked.
   */
  if (session == nullptr) {
    erpc_dprintf("%s: Client session is already disconnected.\n", issue_msg);
    return;
  }

  assert(session->is_client());

  /*
   * If we are here, we still have the requester session as Client.
   *
   * Check if the session state has advanced beyond kConnectInProgress (due to
   * a prior connect response). If so, we are not interested in this response
   * and the callback is not invoked.
   */
  if (session->state > SessionState::kConnectInProgress) {
    /* We may have a disconnect request outstanding */
    if (mgmt_retry_queue_contains(session)) {
      assert(session->state == SessionState::kDisconnectInProgress);
    }

    erpc_dprintf("%s: Ignoring. Client is in state %s.\n", issue_msg,
                 session_state_str(session->state).c_str());
    return;
  }

  /*
   * If we are here, this is the first connect response, so the connect request
   * should be in flight. It's not possible to also have a disconnect request in
   * flight, since disconnect must wait for the first connect response.
   */
  assert(mgmt_retry_queue_contains(session));
  mgmt_retry_queue_remove(session);

  /*
   * If the session was not already disconnected, the session endpoint metadata
   * (hostname, app TID, session num) from the pkt should match our local copy.
   *
   * We don't have the server's session number locally yet, so we cannot use
   * SessionEndpoint comparator to compare server endpoint metadata.
   */
  assert(strcmp(session->server.hostname, sm_pkt->server.hostname) == 0);
  assert(session->server.app_tid == sm_pkt->server.app_tid);
  assert(session->server.session_num == kInvalidSessionNum);

  assert(session->client == sm_pkt->client);

  /*
   * If the connect response has an error, the server has not allocated a
   * Session. Mark the session as disconnected and invoke callback.
   */
  if (sm_pkt->err_type != SessionMgmtErrType::kNoError) {
    erpc_dprintf("%s: Error %s.\n", issue_msg,
                 session_mgmt_err_type_str(sm_pkt->err_type).c_str());

    session->state = SessionState::kDisconnected;
    session_mgmt_handler(session->local_session_num,
                         SessionMgmtEventType::kConnectFailed, sm_pkt->err_type,
                         context);
    bury_session(session);
    return;
  }

  /*
   * If we are here, the server has created a session endpoint.
   *
   * Try to resolve the server's routing information into the packet. If this
   * fails, invoke kConnectFailed callback.
   */
  RoutingInfo *srv_routing_info = &(sm_pkt->server.routing_info);
  erpc_dprintf("eRPC Rpc %u: Resolving server's routing info %s.\n", app_tid,
               TTr::routing_info_str(srv_routing_info).c_str());

  bool resolve_success;
  if (!testing_fail_resolve_remote_rinfo_client) {
    resolve_success = transport->resolve_remote_routing_info(srv_routing_info);
  } else {
    resolve_success = false; /* Inject error for testing */
  }

  if (!resolve_success) {
    erpc_dprintf("%s: Client failed to resolve server routing info.\n",
                 issue_msg);

    /*
     * The server has allocated a Session, so we'll try to free server
     * resources by disconnecting. The user will only get the kConnectFailed
     * callback, i.e., no callback will be invoked when we get the disconnect
     * response.
     *
     * We need to save the server's endpoint metadata from the packet so we
     * can send it in the subsequent disconnect request.
     */
    session->server = sm_pkt->server;

    /* This ensures that we don't invoke the disconnect callback later */
    session->client_info.sm_callbacks_disabled = true;

    /* Do what destroy_session() does with a kConnected session */
    session->state = SessionState::kDisconnectInProgress;
    mgmt_retry_queue_add(session); /* Checks that session is not in flight */

    erpc_dprintf(
        "eRPC Rpc %u: Sending first (callback-less) disconnect request for "
        "session %u, and invoking kConnectFailed callback\n",
        app_tid, session->local_session_num);
    send_disconnect_req_one(session);

    session_mgmt_handler(
        session->local_session_num, SessionMgmtEventType::kConnectFailed,
        SessionMgmtErrType::kRoutingResolutionFailure, context);

    return;
  }

  /* Save server endpoint metadata. This saves the resolved routing info. */
  session->server = sm_pkt->server;
  session->remote_session_num = session->server.session_num;
  session->state = SessionState::kConnected;

  erpc_dprintf("%s: None. Session connected.\n", issue_msg);
  session_mgmt_handler(session->local_session_num,
                       SessionMgmtEventType::kConnected,
                       SessionMgmtErrType::kNoError, context);
}

}  // End ERpc

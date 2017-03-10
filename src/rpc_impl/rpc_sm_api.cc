/**
 * @file rpc_sm_api.cc
 * @brief Rpc session functions that are exposed to the user.
 */

#include <algorithm>

#include "rpc.h"
#include "util/udp_client.h"

namespace ERpc {

/*
 * This function is not on the critical path and is exposed to the user,
 * so the args checking is always enabled (i.e., no asserts).
 */
template <class Transport_>
Session *Rpc<Transport_>::create_session(const char *rem_hostname,
                                         uint8_t rem_app_tid,
                                         uint8_t rem_phy_port) {
  /* Create the basic issue message */
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg, "eRPC Rpc %s: create_session() failed. Issue",
          get_name().c_str());

  /* Check remote fabric port */
  if (rem_phy_port >= kMaxPhyPorts) {
    erpc_dprintf("%s: Invalid remote fabric port %u\n", issue_msg,
                 rem_phy_port);
    return nullptr;
  }

  /* Check remote hostname */
  if (rem_hostname == nullptr || strlen(rem_hostname) > kMaxHostnameLen) {
    erpc_dprintf("%s: Invalid remote hostname.\n", issue_msg);
    return nullptr;
  }

  /* Creating a session to one's own Rpc as the client is not allowed */
  if (strcmp(rem_hostname, nexus->hostname) == 0 && rem_app_tid == app_tid) {
    erpc_dprintf("%s: Remote Rpc is same as local.\n", issue_msg);
    return nullptr;
  }

  /* Creating two sessions as client to the same remote Rpc is not allowed */
  for (Session *existing_session : session_vec) {
    if (existing_session == nullptr) {
      continue;
    }

    if (strcmp(existing_session->server.hostname, rem_hostname) == 0 &&
        existing_session->server.app_tid == rem_app_tid) {
      /*
       * existing_session->server != this Rpc, since existing_session->server
       * matches (rem_hostname, rem_app_tid), which does match this
       * Rpc (checked earlier). So we must be the client.
       */
      assert(existing_session->is_client());
      erpc_dprintf("%s: Session to %s already exists.\n", issue_msg,
                   existing_session->server.rpc_name().c_str());
      return nullptr;
    }
  }

  /* Ensure bounded session_vec size */
  if (session_vec.size() >= kMaxSessionsPerThread) {
    erpc_dprintf("%s: Session limit (%zu) reached.\n", issue_msg,
                 kMaxSessionsPerThread);
    return nullptr;
  }

  /* Create a new session and fill prealloc MsgBuffers. XXX: Use pool? */
  Session *session =
      new Session(Session::Role::kClient, SessionState::kConnectInProgress);
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    MsgBuffer &msgbuf_i = session->sslot_arr[i].app_resp.pre_resp_msgbuf;
    msgbuf_i = alloc_msg_buffer(Transport_::kMaxDataPerPkt);

    if (msgbuf_i.buf == nullptr) {
      /*
       * We haven't assigned a session number or allocated non-prealloc
       * MsgBuffers yet, so just free prealloc MsgBuffers 0 -- (i - 1).
       */
      for (size_t j = 0; j < i; j++) {
        MsgBuffer &msgbuf_j = session->sslot_arr[j].app_resp.pre_resp_msgbuf;
        assert(msgbuf_j.buf != nullptr);
        free_msg_buffer(msgbuf_j);
      }

      erpc_dprintf("%s: Failed to allocate prealloc MsgBuffer.\n", issue_msg);
      return nullptr;
    }
  }

  /*
   * Fill in client and server endpoint metadata. Commented server fields will
   * be filled when the connect response is received.
   */
  SessionEndpoint &client_endpoint = session->client;

  client_endpoint.transport_type = transport->transport_type;
  strcpy((char *)client_endpoint.hostname, nexus->hostname);
  client_endpoint.phy_port = phy_port;
  client_endpoint.app_tid = app_tid;
  client_endpoint.session_num = session_vec.size();
  client_endpoint.secret = slow_rand.next_u64() & ((1ull << kSecretBits) - 1);
  transport->fill_local_routing_info(&client_endpoint.routing_info);

  SessionEndpoint &server_endpoint = session->server;
  server_endpoint.transport_type = transport->transport_type;
  strcpy((char *)server_endpoint.hostname, rem_hostname);
  server_endpoint.phy_port = rem_phy_port;
  server_endpoint.app_tid = rem_app_tid;
  // server_endpoint.session_num = ??
  server_endpoint.secret = client_endpoint.secret; /* Secret is shared */
  // server_endpoint.routing_info = ??

  session_vec.push_back(session); /* Add to list of all sessions */
  mgmt_retry_queue_add(session);  /* Record management request for retry */
  send_connect_req_one(session);

  return session;
}

template <class Transport_>
bool Rpc<Transport_>::destroy_session(Session *session) {
  if (session == nullptr || !session->is_client()) {
    erpc_dprintf("eRPC Rpc %s: destroy_session() failed. Invalid session.\n",
                 get_name().c_str());
    return false;
  }

  uint16_t session_num = session->client.session_num;
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %s: destroy_session() failed for session %u. Issue",
          get_name().c_str(), session_num);

  switch (session->state) {
    case SessionState::kConnectInProgress:
      /* Can't disconnect right now. User needs to wait. */
      assert(mgmt_retry_queue_contains(session));
      erpc_dprintf("%s: Session connection in progress.\n", issue_msg);
      return false;

    case SessionState::kConnected:
    case SessionState::kErrorServerEndpointExists:
      session->state = SessionState::kDisconnectInProgress;
      mgmt_retry_queue_add(session); /* Ensures that session is not in flight */
      send_disconnect_req_one(session);
      return true;

    case SessionState::kDisconnectInProgress:
      assert(mgmt_retry_queue_contains(session));
      erpc_dprintf("%s: Session disconnection in progress.\n", issue_msg);
      return false;

    case SessionState::kDisconnected:
      assert(!mgmt_retry_queue_contains(session));
      erpc_dprintf_noargs(
          "eRPC Rpc: destroy_session() failed. Issue: "
          "Session already destroyed.\n");
      return false;

    case SessionState::kErrorServerEndpointAbsent:
      /*
       * This happens when either we get a connect response containing an error,
       * or when the connect request times out. In either case, we don't send a
       * disconnect request. In case of a timeout, we leak memory at the server.
       */
      assert(!mgmt_retry_queue_contains(session));
      erpc_dprintf(
          "eRPC Rpc %s: destroy_session() succeeded for error-ed session %u.\n",
          get_name().c_str(), session_num);

      session_mgmt_handler(session, SessionMgmtEventType::kDisconnected,
                           SessionMgmtErrType::kNoError, context);
      bury_session(session);
      return true;
  }
  exit(-1);
  return false;
}

template <class Transport_>
size_t Rpc<Transport_>::num_active_sessions() {
  size_t ret = 0;
  for (Session *session : session_vec) {
    if (session != nullptr) {
      ret++;
    }
  }

  return ret;
}

}  // End ERpc

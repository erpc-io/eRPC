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
Session *Rpc<Transport_>::create_session(size_t local_fdev_port_index,
                                         const char *rem_hostname,
                                         size_t rem_app_tid,
                                         size_t rem_fdev_port_index) {
  /* Create the basic issue message */
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg, "eRPC Rpc: create_session failed. Issue");

  /* Check local fabric port */
  if (local_fdev_port_index >= kMaxFabDevPorts) {
    erpc_dprintf("%s: Invalid local fabric port %zu\n", issue_msg,
                 local_fdev_port_index);
    return nullptr;
  }

  /* Check remote fabric port */
  if (rem_fdev_port_index >= kMaxFabDevPorts) {
    erpc_dprintf("%s: Invalid remote fabric port %zu\n", issue_msg,
                 rem_fdev_port_index);
    return nullptr;
  }

  /* Ensure that the requested local port is managed by Rpc */
  if (!is_fdev_port_managed(local_fdev_port_index)) {
    erpc_dprintf("%s: eRPC Rpc: Local fabric port %zu unmanaged.\n", issue_msg,
                 local_fdev_port_index);
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
       * @existing_session->server != this Rpc, since @existing_session->server
       * matches (@rem_hostname, @rem_app_tid), which does match this
       * Rpc (checked earlier). So we must be the client.
       */
      assert(existing_session->role == Session::Role::kClient);
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

  /* Create a new session. XXX: Use pool? */
  Session *session =
      new Session(Session::Role::kClient, SessionState::kConnectInProgress);

  /*
   * Fill in client and server metadata. Commented server fields will be filled
   * when the connect response is received.
   */
  SessionMetadata &client_metadata = session->client;

  client_metadata.transport_type = transport->transport_type;
  strcpy((char *)client_metadata.hostname, nexus->hostname);
  client_metadata.app_tid = app_tid;
  client_metadata.fdev_port_index = local_fdev_port_index;
  client_metadata.session_num = session_vec.size();
  client_metadata.start_seq = generate_start_seq();
  transport->fill_routing_info(&client_metadata.routing_info);

  SessionMetadata &server_metadata = session->server;
  server_metadata.transport_type = transport->transport_type;
  strcpy((char *)server_metadata.hostname, rem_hostname);
  server_metadata.app_tid = rem_app_tid;
  server_metadata.fdev_port_index = rem_fdev_port_index;
  // server_metadata.session_num = ??
  // server_metadata.start_seq = ??
  // server_metadata.routing_info = ??

  session_vec.push_back(session); /* Add to list of all sessions */
  add_to_in_flight(session); /* Add to list of sessions w/ in-flight sm reqs */
  send_connect_req_one(session);

  return session;
}

template <class Transport_>
bool Rpc<Transport_>::destroy_session(Session *session) {
  /* Create the basic issue message */
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg, "eRPC Rpc: destroy_session() failed. Issue");

  if (!is_session_ptr_client(session)) {
    erpc_dprintf("%s: Invalid session.\n", issue_msg);
    return false;
  }

  switch (session->state) {
    case SessionState::kConnectInProgress:
      /* Can't disconnect right now. User needs to wait. */
      assert(is_in_flight(session));
      erpc_dprintf("%s: Session connection in progress.\n", issue_msg);
      return false;

    case SessionState::kConnected:
      /* This is the only case where we send the disconnect packet */
      session->state = SessionState::kDisconnectInProgress;
      add_to_in_flight(session); /* Ensures that @session is not in flight */
      send_disconnect_req_one(session);
      return true;

    case SessionState::kDisconnectInProgress:
      assert(is_in_flight(session));
      erpc_dprintf("%s: Session disconnection in progress.\n", issue_msg);
      return false;

    case SessionState::kDisconnected:
      assert(!is_in_flight(session));
      erpc_dprintf("%s: Session already disconnected.\n", issue_msg);
      return false;

    case SessionState::kError:
      /*
       * This means that the connect request timed out, so we don't send a
       * disconnect request.
       */
      session_mgmt_handler(session, SessionMgmtEventType::kDisconnected,
                           SessionMgmtErrType::kNoError, context);
      bury_session(session);
      return true;
  }
  exit(-1);
  return false;
}

}  // End ERpc

#include <algorithm>

#include "rpc.h"
#include "util/udp_client.h"

namespace ERpc {

template <class Transport_>
Rpc<Transport_>::Rpc(Nexus *nexus, void *context, size_t app_tid,
                     session_mgmt_handler_t session_mgmt_handler,
                     std::vector<size_t> fdev_port_vec)
    : nexus(nexus),
      context(context),
      app_tid(app_tid),
      session_mgmt_handler(session_mgmt_handler),
      num_fdev_ports(fdev_port_vec.size()) {
  if (fdev_port_vec.size() == 0) {
    fprintf(stderr, "eRPC Rpc: FATAL. Rpc created with 0 fabric ports.\n");
    exit(-1);
  }

  if (fdev_port_vec.size() > kMaxFabDevPorts) {
    fprintf(stderr, "eRPC Rpc: FATAL. Only %zu local ports supported.\n",
            kMaxFabDevPorts);
    exit(-1);
  }

  /* Record the requested local ports in an array */
  int i = 0;
  for (size_t fdev_port : fdev_port_vec) {
    fdev_port_arr[i] = fdev_port;
    i++;
  }

  /* Initialize the transport */
  transport = new Transport_();

  /* Register a hook with the Nexus */
  sm_hook.app_tid = app_tid;
  nexus->register_hook((SessionMgmtHook *)&sm_hook);
}

template <class Transport_>
Rpc<Transport_>::~Rpc() {
  for (Session *session : session_vec) {
    if (session != nullptr) {
      /* Free this session */
      _unused(session);
    }
  }
}

template <class Transport_>
uint64_t Rpc<Transport_>::generate_start_seq() {
  uint64_t rand = slow_rand.next_u64();
  return (rand & kStartSeqMask);
}

template <class Transport_>
bool Rpc<Transport_>::is_session_managed(Session *session) {
  assert(session != NULL);

  if (std::find(session_vec.begin(), session_vec.end(), session) ==
      session_vec.end()) {
    return false;
  }

  return true;
}

/**
 * @brief Create a new client session with this Rpc.
 *
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

  /* Create a new session and add it to the session list. XXX: Use pool? */
  Session *session = new Session(Session::Role::kClient, SessionState::kInit);

  /* Fill in local metadata */
  SessionMetadata &client_metadata = session->client;

  client_metadata.transport_type = transport->transport_type;
  strcpy((char *)client_metadata.hostname, nexus->hostname);
  client_metadata.app_tid = app_tid;
  client_metadata.fdev_port_index = local_fdev_port_index;
  client_metadata.session_num = (uint32_t)session_vec.size();
  client_metadata.start_seq = generate_start_seq();
  transport->fill_routing_info(&client_metadata.routing_info);

  /*
   * Fill in remote metadata. Commented fields will be filled when the
   * session is connected.
   */
  SessionMetadata &server_metadata = session->server;
  server_metadata.transport_type = transport->transport_type;
  strcpy((char *)server_metadata.hostname, rem_hostname);
  server_metadata.app_tid = rem_app_tid;
  server_metadata.fdev_port_index = rem_fdev_port_index;
  // server_metadata.session_num = ??
  // server_metadata.start_seq = ??
  // server_metadata.routing_info = ??

  session_vec.push_back(session);
  return session;
}

template <class Transport_>
bool Rpc<Transport_>::connect_session(Session *session) {
  assert(session != NULL);

  if (!is_session_managed(session)) {
    erpc_dprintf_noargs(
        "eRPC Rpc: connect_session failed. Session does not belong to Rpc.\n");
    return false;
  }

  if (session->role != Session::Role::kClient) {
    erpc_dprintf_noargs(
        "eRPC Rpc: connect_session failed. Session role is not Client.\n");
    return false;
  }

  if (session->state != SessionState::kInit) {
    erpc_dprintf_noargs(
        "eRPC Rpc: connect_session failed. Session status is not Init.\n");
    return false;
  }

  session->state = SessionState::kConnectInProgress;

  SessionMgmtPkt connect_req(SessionMgmtPktType::kConnectReq);
  memcpy((void *)&connect_req.client, (void *)&session->client,
         sizeof(connect_req.client));
  memcpy((void *)&connect_req.server, (void *)&session->server,
         sizeof(connect_req.server));

  connect_req.send_to(session->server.hostname, &nexus->udp_config);
  return true;
}

template <class Transport_>
std::string Rpc<Transport_>::get_name() {
  std::string ret;
  ret += std::string("[");
  ret += std::string(nexus->hostname);
  ret += std::string(", ");
  ret += std::to_string(app_tid);
  ret += std::string("]");
  return ret;
}

template <class Transport_>
bool Rpc<Transport_>::is_fdev_port_managed(size_t fab_port_index) {
  for (size_t i = 0; i < num_fdev_ports; i++) {
    if (fdev_port_arr[i] == fab_port_index) {
      return true;
    }
  }
  return false;
}

template <class Transport_>
void Rpc<Transport_>::send_request(const Session *session,
                                   const Buffer *buffer) {
  _unused(session);
  _unused(buffer);
}

template <class Transport_>
void Rpc<Transport_>::send_response(const Session *session,
                                    const Buffer *buffer) {
  _unused(session);
  _unused(buffer);
};

}  // End ERpc

#include <algorithm>

#include "rpc.h"
#include "util/udp_client.h"

namespace ERpc {

template <class Transport_>
Rpc<Transport_>::Rpc(Nexus *nexus, void *context, int app_tid,
                     session_mgmt_handler_t session_mgmt_handler,
                     std::vector<int> fdev_port_vec)
    : nexus(nexus),
      context(context),
      app_tid(app_tid),
      session_mgmt_handler(session_mgmt_handler),
      num_fdev_ports((int)fdev_port_vec.size()),
      next_session_num(0) {
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
  for (int fdev_port : fdev_port_vec) {
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
    /* Free this session */
    _unused(session);
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

template <class Transport_>
Session *Rpc<Transport_>::create_session(int local_fdev_port_index,
                                         const char *rem_hostname,
                                         int rem_app_tid,
                                         int rem_fdev_port_index) {
  /*
   * This function is not on the critical path and is exposed to the user,
   * so the args checking is always enabled (i.e., no asserts).
   */
  if (local_fdev_port_index < 0 ||
      local_fdev_port_index >= (int)kMaxFabDevPorts) {
    fprintf(stderr, "eRPC create_session: FATAL. Invalid local port index.\n");
    exit(-1);
  }

  if (rem_hostname == nullptr || strlen(rem_hostname) > kMaxHostnameLen) {
    fprintf(stderr, "eRPC create_session: FATAL. Invalid remote hostname.\n");
    exit(-1);
  }

  if (rem_app_tid < 0) {
    fprintf(stderr, "eRPC create_session: FATAL. Invalid remote app TID.\n");
    exit(-1);
  }

  if (rem_fdev_port_index < 0 || rem_fdev_port_index >= (int)kMaxFabDevPorts) {
    fprintf(stderr, "eRPC create_session: FATAL. Invalid remote port index.\n");
    exit(-1);
  }

  /* Ensure that the requested local port is managed by Rpc */
  bool is_local_port_managed = false;
  for (int i = 0; i < num_fdev_ports; i++) {
    if (fdev_port_arr[i] == local_fdev_port_index) {
      is_local_port_managed = true;
    }
  }

  if (!is_local_port_managed) {
    fprintf(stderr,
            "eRPC create_session: FATAL. Local port index %d is unmanaged\n",
            local_fdev_port_index);
    exit(-1);
  }

  Session *session = new Session(); /* XXX: Use pool? */

  /* Fill in local metadata */
  SessionMetadata &client_metadata = session->client;

  client_metadata.transport_type = transport->transport_type;
  strcpy((char *)client_metadata.hostname, nexus->hostname);
  client_metadata.app_tid = app_tid;
  client_metadata.fdev_port_index = local_fdev_port_index;
  client_metadata.session_num = next_session_num++; /* Assign unique sess num */
  client_metadata.start_seq = generate_start_seq();
  transport->fill_routing_info(&client_metadata.routing_info);

  /*
   * Fill in remote metadata. Commented fields will be filled when the
   * session is connected.
   */
  SessionMetadata &server_metadata = session->server;
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
void Rpc<Transport_>::connect_session(Session *session) {
  assert(session != NULL);

  if (!is_session_managed(session)) {
    fprintf(stderr,
            "eRPC connect_session: FATAL. Session %p is not managed Rpc.\n",
            session);
    exit(-1);
  }

  /* Send a connect request */
  UDPClient *udp_client =
      new UDPClient(session->server.hostname, nexus->global_udp_port);

  SessionMgmtPkt connect_req(SessionMgmtPktType::kConnectReq);
  memcpy((void *)&connect_req.client, (void *)&session->client,
         sizeof(connect_req.client));
  memcpy((void *)&connect_req.server, (void *)&session->server,
         sizeof(connect_req.server));

  ssize_t ret = udp_client->send((char *)&connect_req, sizeof(connect_req));
  assert(ret == sizeof(connect_req));
  _unused(ret);

  delete udp_client;
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

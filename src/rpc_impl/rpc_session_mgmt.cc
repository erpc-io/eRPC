#include "rpc.h"
#include <algorithm>

namespace ERpc {

/**
 * @brief Process all session management events in the queue.
 */
template <class Transport_>
void Rpc<Transport_>::handle_session_management() {
  assert(sm_hook.session_mgmt_ev_counter > 0);
  sm_hook.session_mgmt_mutex.lock();

  /* Handle all session management requests */
  for (SessionMgmtPkt *sm_pkt : sm_hook.session_mgmt_pkt_list) {
    erpc_dprintf("eRPC Rpc: Rpc %d received session mgmt pkt of type %s\n",
                 app_tid, session_mgmt_pkt_type_str(sm_pkt->pkt_type).c_str());

    /* The sender of a packet cannot be this Rpc */
    if (is_session_mgmt_pkt_type_req(sm_pkt->pkt_type)) {
      assert(!(strcmp(sm_pkt->client.hostname, nexus->hostname) &&
             sm_pkt->client.app_tid == app_tid));
    } else {
      assert(!(strcmp(sm_pkt->server.hostname, nexus->hostname) &&
             sm_pkt->server.app_tid == app_tid));
    }

    switch (sm_pkt->pkt_type) {
      case SessionMgmtPktType::kConnectReq:
        handle_session_connect_req(sm_pkt);
        break;
      case SessionMgmtPktType::kConnectResp:
        handle_session_connect_resp(sm_pkt);
        break;
      case SessionMgmtPktType::kDisconnectReq:
        handle_session_connect_resp(sm_pkt);
        break;
      case SessionMgmtPktType::kDisconnectResp:
        handle_session_connect_resp(sm_pkt);
        break;
      default:
        assert(false);
        break;
    }
    free(sm_pkt);
  }

  sm_hook.session_mgmt_pkt_list.clear();
  sm_hook.session_mgmt_ev_counter = 0;
  sm_hook.session_mgmt_mutex.unlock();
};

/**
 * @brief Handle a session connect request. Caller will free \p sm_pkt.
 */
template <class Transport_>
void Rpc<Transport_>::handle_session_connect_req(SessionMgmtPkt *sm_pkt) {
  assert(sm_pkt != NULL);
  assert(sm_pkt->pkt_type == SessionMgmtPktType::kConnectReq);

  /* Ensure that the server fields were filled correctly */
  assert(sm_pkt->server.app_tid == app_tid);
  assert(strcmp(sm_pkt->server.hostname, nexus->hostname));

  /* Check if the requested fabric port is managed by us */
  if (!is_fdev_port_managed(sm_pkt->server.fdev_port_index)) {
    erpc_dprintf(
        "eRPC Rpc: Rpc %s received session connect request from [%s, %d] with "
        "invalid server fabric port %d\n",
        get_name().c_str(), sm_pkt->client.hostname, sm_pkt->client.app_tid,
        sm_pkt->server.fdev_port_index);

    sm_pkt->send_resp_mut(nexus->global_udp_port,
                          SessionMgmtResponseType::kInvalidRemotePort);
    return;
  }

  /* Check that the transport matches */
  if (sm_pkt->server.transport_type != transport->transport_type) {
    erpc_dprintf(
        "eRPC Rpc: Rpc %s received session connect request from [%s, %d] with "
        "invalid transport type %s\n",
        get_name().c_str(), sm_pkt->client.hostname, sm_pkt->client.app_tid,
        get_transport_name(sm_pkt->server.transport_type).c_str());

    sm_pkt->send_resp_mut(nexus->global_udp_port,
                          SessionMgmtResponseType::kInvalidTransport);
    return;
  }

  /*
   * Check if we (= this Rpc) already have a session as the server with the
   * client Rpc (C) that sent this packet. It's OK if we have a session where
   * we are the client Rpc, and C is the server Rpc.
   */
  for (Session *existing_session : session_vec) {
    /*
     * This check ensures that we own the session as the server.
     *
     * If the check succeeds, we cannot own @existing_session as the client:
     * @sm_pkt was sent by a different Rpc than us, since an Rpc cannot send
     * session management packets to itself. So the client hostname and app_tid
     * in the located session cannot be ours, since they are same as @sm_pkt's.
     */
    if ((existing_session != nullptr) &&
        strcmp(existing_session->client.hostname, sm_pkt->client.hostname) &&
        (existing_session->client.app_tid == sm_pkt->client.app_tid)) {
      assert(existing_session->role == Session::Role::kServer);
      assert(existing_session->status == SessionStatus::kConnected);

      /* There's a valid session, so client's metadata cannot have changed. */
      assert(memcmp((void *)&existing_session->client, (void *)&sm_pkt->client,
                    sizeof(existing_session->client)) == 0);

      erpc_dprintf(
          "eRPC Rpc: Rpc %s received duplicate session connect "
          "request from %s\n",
          get_name().c_str(), existing_session->get_client_name().c_str());

      /* Send a connect success response */
      sm_pkt->server = existing_session->server; /* Fill in server metadata */
      sm_pkt->send_resp_mut(nexus->global_udp_port,
                            SessionMgmtResponseType::kConnectSuccess);
      return;
    }
  }

  /* Check if we are allowed to create another session */
  if (session_vec.size() == kMaxSessionsPerThread) {
    erpc_dprintf(
        "eRPC Rpc: Rpc %s received session connect request from [%s, %d], "
        "but we are at session limit (%zu)\n",
        get_name().c_str(), sm_pkt->client.hostname, sm_pkt->client.app_tid,
        kMaxSessionsPerThread);

    sm_pkt->send_resp_mut(nexus->global_udp_port,
                          SessionMgmtResponseType::kTooManySessions);
    return;
  }

  /* If we are here, it's OK to create a new session */
  Session *session =
      new Session(Session::Role::kServer, SessionStatus::kConnected);

  /* Set the server metadata fields in the packet */
  sm_pkt->server.session_num = (uint32_t)session_vec.size();
  sm_pkt->server.start_seq = generate_start_seq();
  transport->fill_routing_info(&(sm_pkt->server.routing_info));

  /* Copy the packet's metadata to the created session */
  session->server = sm_pkt->server;
  session->client = sm_pkt->client;

  sm_pkt->send_resp_mut(nexus->global_udp_port,
                        SessionMgmtResponseType::kConnectSuccess);
  return;
}

template <class Transport_>
void Rpc<Transport_>::handle_session_connect_resp(SessionMgmtPkt *sm_pkt) {
  _unused(sm_pkt);
}

template <class Transport_>
void Rpc<Transport_>::handle_session_disconnect_req(SessionMgmtPkt *sm_pkt) {
  _unused(sm_pkt);
}

template <class Transport_>
void Rpc<Transport_>::handle_session_disconnect_resp(SessionMgmtPkt *sm_pkt) {
  _unused(sm_pkt);
}

}  // End ERpc

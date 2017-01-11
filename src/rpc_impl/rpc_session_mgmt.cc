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
        break;
    }
    free(sm_pkt);
  }

  sm_hook.session_mgmt_pkt_list.clear();
  sm_hook.session_mgmt_ev_counter = 0;
  sm_hook.session_mgmt_mutex.unlock();
};

template <class Transport_>
void Rpc<Transport_>::handle_session_connect_req(SessionMgmtPkt *sm_pkt) {
  assert(sm_pkt != NULL);
  assert(sm_pkt->pkt_type == SessionMgmtPktType::kConnectReq);

  /* Ensure that the server fields were filled correctly */
  assert(sm_pkt->server.app_tid == app_tid);
  assert(strcmp(sm_pkt->server.hostname, nexus->hostname));

  /* Send back an error if we don't manage the requested fabric port */
  if (!is_fdev_port_managed(sm_pkt->server.fdev_port_index)) {
    sm_pkt->pkt_type = session_mgmt_pkt_type_req_to_resp(sm_pkt->pkt_type);
    sm_pkt->resp_type = SessionMgmtResponseType::kInvalidRemotePort;

    sm_pkt->send_to(sm_pkt->client.hostname, nexus->global_udp_port);

    delete sm_pkt;
    return;
  }

  /* Check if we already have a session with this client */
  for (Session *existing_session : session_vec) {
    if (strcmp(existing_session->client.hostname, sm_pkt->client.hostname) &&
        existing_session->client.app_tid == sm_pkt->client.app_tid) {
      assert(existing_session->client.session_num ==
             sm_pkt->client.session_num);

      erpc_dprintf(
          "eRPC Rpc: Rpc %s received duplicate session connect "
          "request from %s\n",
          get_name().c_str(), existing_session->get_client_name().c_str());

      sm_pkt->pkt_type = session_mgmt_pkt_type_req_to_resp(sm_pkt->pkt_type);
      sm_pkt->resp_type = SessionMgmtResponseType::kSessionExists;

      sm_pkt->send_to(sm_pkt->client.hostname, nexus->global_udp_port);

      delete sm_pkt;
      return;
    }
  }

  /* Create a new session. XXX: Use pool? */
  Session *session = new Session();
  session->client = sm_pkt->client;
  session->server = sm_pkt->server;
  _unused(session);
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

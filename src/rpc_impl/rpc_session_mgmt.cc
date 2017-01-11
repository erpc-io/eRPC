#include "rpc.h"

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
void Rpc<Transport_>::handle_session_connect_req(SessionMgmtPkt *pkt) {
  assert(pkt != NULL);
  assert(pkt->pkt_type == SessionMgmtPktType::kConnectReq);
  assert(pkt->server.app_tid == app_tid);
  assert(strcmp(pkt->server.hostname, nexus->hostname));
}

template <class Transport_>
void Rpc<Transport_>::handle_session_connect_resp(SessionMgmtPkt *pkt) {
  _unused(pkt);
}

template <class Transport_>
void Rpc<Transport_>::handle_session_disconnect_req(SessionMgmtPkt *pkt) {
  _unused(pkt);
}

template <class Transport_>
void Rpc<Transport_>::handle_session_disconnect_resp(SessionMgmtPkt *pkt) {
  _unused(pkt);
}

}  // End ERpc

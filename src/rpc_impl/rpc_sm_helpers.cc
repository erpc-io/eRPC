/*
 * @file rpc_sm_helpers.cc
 * @brief Session management helper methods
 */
#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::bury_session_st(Session *session) {
  assert(in_creator());
  assert(session != nullptr);

  if (session->is_client()) {
    assert(!session->client_info.sm_request_pending);
  }

  // Free session resources
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    // Free the preallocated MsgBuffer
    MsgBuffer &msg_buf = session->sslot_arr[i].pre_resp_msgbuf;
    free_msg_buffer(msg_buf);

    // XXX: Which other MsgBuffers do we need to free? Which MsgBuffers are
    // guaranteed to have been freed at this point?
  }

  // No need to lock the session to nullify it
  session_vec.at(session->local_session_num) = nullptr;
  delete session;  // This does nothing
}

template <class TTr>
void Rpc<TTr>::handle_session_management_st() {
  assert(in_creator());
  assert(nexus_hook.sm_rx_list.size > 0);
  nexus_hook.sm_rx_list.lock();

  // Handle all session management requests
  for (typename Nexus<TTr>::SmWorkItem &wi : nexus_hook.sm_rx_list.list) {
    SessionMgmtPkt *sm_pkt = wi.sm_pkt;
    assert(sm_pkt != nullptr);
    assert(session_mgmt_pkt_type_is_valid(sm_pkt->pkt_type));

    // The sender of a packet cannot be this Rpc
    if (sm_pkt->is_req()) {
      assert(!(strcmp(sm_pkt->client.hostname, nexus->hostname.c_str()) == 0 &&
               sm_pkt->client.rpc_id == rpc_id));
    } else {
      assert(!(strcmp(sm_pkt->server.hostname, nexus->hostname.c_str()) == 0 &&
               sm_pkt->server.rpc_id == rpc_id));
    }

    switch (sm_pkt->pkt_type) {
      case SessionMgmtPktType::kConnectReq:
        handle_connect_req_st(&wi);
        break;
      case SessionMgmtPktType::kConnectResp:
        handle_connect_resp_st(sm_pkt);
        break;
      case SessionMgmtPktType::kDisconnectReq:
        handle_disconnect_req_st(&wi);
        break;
      case SessionMgmtPktType::kDisconnectResp:
        handle_disconnect_resp_st(sm_pkt);
        break;
      default:
        assert(false);
        break;
    }

    // Free the packet memory allocated by the SM thread
    delete sm_pkt;
  }

  // Clear the session management RX list
  nexus_hook.sm_rx_list.locked_clear();
  nexus_hook.sm_rx_list.unlock();
}

template <class TTr>
void Rpc<TTr>::enqueue_sm_req(Session *session, SessionMgmtPktType pkt_type) {
  assert(session != nullptr && session->is_client());

  SessionMgmtPkt *sm_pkt = new SessionMgmtPkt();  // Freed by SM thread
  sm_pkt->pkt_type = pkt_type;
  sm_pkt->client = session->client;
  sm_pkt->server = session->server;
  nexus_hook.sm_tx_list->unlocked_push_back(
      typename Nexus<TTr>::SmWorkItem(rpc_id, sm_pkt, nullptr));
}

template <class TTr>
void Rpc<TTr>::enqueue_sm_resp(typename Nexus<TTr>::SmWorkItem *req_wi,
                               SessionMgmtErrType err_type) {
  assert(req_wi != nullptr);
  assert(req_wi->peer != nullptr);

  SessionMgmtPkt *req_sm_pkt = req_wi->sm_pkt;
  assert(req_sm_pkt->is_req());

  // Copy the request - this gets freed by the SM thread
  auto *resp_sm_pkt = new SessionMgmtPkt();
  *resp_sm_pkt = *req_sm_pkt;

  // Change the packet type to response
  resp_sm_pkt->pkt_type =
      session_mgmt_pkt_type_req_to_resp(req_sm_pkt->pkt_type);
  resp_sm_pkt->err_type = err_type;

  typename Nexus<TTr>::SmWorkItem wi(rpc_id, resp_sm_pkt, req_wi->peer);
  nexus_hook.sm_tx_list->unlocked_push_back(wi);
}

}  // End ERpc

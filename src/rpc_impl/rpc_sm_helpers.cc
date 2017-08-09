/*
 * @file rpc_sm_helpers.cc
 * @brief Session management helper methods
 */
#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::handle_sm_st() {
  assert(in_creator());
  assert(nexus_hook.sm_rx_list.size > 0);
  nexus_hook.sm_rx_list.lock();

  // Handle SM work items queued by the session management thread
  for (const typename Nexus<TTr>::SmWorkItem &wi : nexus_hook.sm_rx_list.list) {
    // Reset work items don't have a valid SM packet, so handle them first
    if (wi.is_reset()) {
      assert(wi.reset_rem_hostname.length() > 0);
      handle_reset_st(wi.reset_rem_hostname);
      continue;
    }

    const SmPkt &sm_pkt = wi.sm_pkt;
    assert(sm_pkt_type_is_valid(sm_pkt.pkt_type));

    // The sender of a packet cannot be this Rpc
    if (sm_pkt.is_req()) {
      assert(!(strcmp(sm_pkt.client.hostname, nexus->hostname.c_str()) == 0 &&
               sm_pkt.client.rpc_id == rpc_id));
    } else {
      assert(!(strcmp(sm_pkt.server.hostname, nexus->hostname.c_str()) == 0 &&
               sm_pkt.server.rpc_id == rpc_id));
    }

    switch (sm_pkt.pkt_type) {
      case SmPktType::kConnectReq:
        handle_connect_req_st(wi);
        break;
      case SmPktType::kConnectResp:
        handle_connect_resp_st(sm_pkt);
        break;
      case SmPktType::kDisconnectReq:
        handle_disconnect_req_st(wi);
        break;
      case SmPktType::kDisconnectResp:
        handle_disconnect_resp_st(sm_pkt);
        break;
      case SmPktType::kFaultResetPeerReq:
        // This is handled in the Nexus
        throw std::runtime_error("Invalid packet type");
    }
  }

  // Clear the session management RX list
  nexus_hook.sm_rx_list.locked_clear();
  nexus_hook.sm_rx_list.unlock();
}

template <class TTr>
void Rpc<TTr>::bury_session_st(Session *session) {
  assert(in_creator());
  assert(session != nullptr);
  assert(session->state == SessionState::kDisconnected);

  if (session->is_client()) {
    assert(!session->client_info.sm_api_req_pending);
  }

  // Free session resources
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    // Free the preallocated MsgBuffer
    MsgBuffer &msg_buf = session->sslot_arr[i].pre_resp_msgbuf;
    free_msg_buffer(msg_buf);

    // XXX: Which other MsgBuffers do we need to free? Which MsgBuffers are
    // guaranteed to have been freed at this point?
  }

  session_vec.at(session->local_session_num) = nullptr;
  delete session;  // This does nothing except free the session memory
}

template <class TTr>
void Rpc<TTr>::enqueue_sm_req_st(Session *session, SmPktType pkt_type) {
  assert(in_creator());
  assert(session != nullptr && session->is_client());

  SmPkt sm_pkt;
  sm_pkt.pkt_type = pkt_type;
  sm_pkt.client = session->client;
  sm_pkt.server = session->server;

  nexus_hook.sm_tx_list->unlocked_push_back(
      typename Nexus<TTr>::SmWorkItem(rpc_id, sm_pkt));
}

template <class TTr>
void Rpc<TTr>::enqueue_sm_resp_st(const typename Nexus<TTr>::SmWorkItem &req_wi,
                                  SmErrType err_type) {
  assert(in_creator());
  assert(req_wi.sm_pkt.is_req());

  SmPkt sm_pkt = req_wi.sm_pkt;
  sm_pkt.pkt_type = sm_pkt_type_req_to_resp(sm_pkt.pkt_type);  // Change to resp
  sm_pkt.err_type = err_type;

  nexus_hook.sm_tx_list->unlocked_push_back(
      typename Nexus<TTr>::SmWorkItem(rpc_id, sm_pkt));
}

}  // End ERpc

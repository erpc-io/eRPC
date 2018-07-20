/*
 * @file rpc_sm_helpers.cc
 * @brief Session management helper methods
 */
#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::handle_sm_rx_st() {
  assert(in_dispatch());
  MtQueue<SmPkt> &queue = nexus_hook.sm_rx_queue;

  while (queue.size > 0) {
    const SmPkt sm_pkt = queue.unlocked_pop();  // Lock is held only briefly
    switch (sm_pkt.pkt_type) {
      case SmPktType::kConnectReq:
        handle_connect_req_st(sm_pkt);
        break;
      case SmPktType::kConnectResp:
        handle_connect_resp_st(sm_pkt);
        break;
      case SmPktType::kDisconnectReq:
        handle_disconnect_req_st(sm_pkt);
        break;
      case SmPktType::kDisconnectResp:
        handle_disconnect_resp_st(sm_pkt);
        break;
      default:
        throw std::runtime_error("Invalid packet type");
    }
  }
}

template <class TTr>
void Rpc<TTr>::bury_session_st(Session *session) {
  assert(in_dispatch());

  // Free session resources
  //
  // XXX: Which other MsgBuffers do we need to free? Which MsgBuffers are
  // guaranteed to have been freed at this point?

  if (session->is_server()) {
    for (const SSlot &sslot : session->sslot_arr) {
      free_msg_buffer(sslot.pre_resp_msgbuf);  // Prealloc buf is always valid
    }
  }

  session_vec.at(session->local_session_num) = nullptr;
  delete session;  // This does nothing except free the session memory
}

template <class TTr>
void Rpc<TTr>::sm_pkt_udp_tx_st(const SmPkt &sm_pkt) {
  LOG_INFO("Rpc %u: Sending packet %s.\n", rpc_id, sm_pkt.to_string().c_str());
  const std::string rem_hostname =
      sm_pkt.is_req() ? sm_pkt.server.hostname : sm_pkt.client.hostname;
  const uint16_t rem_sm_udp_port =
      sm_pkt.is_req() ? sm_pkt.server.sm_udp_port : sm_pkt.client.sm_udp_port;

  udp_client.send(rem_hostname, rem_sm_udp_port, sm_pkt);
}

template <class TTr>
void Rpc<TTr>::send_sm_req_st(Session *session) {
  assert(in_dispatch() && session->is_client());
  assert(session->state == SessionState::kConnectInProgress ||
         session->state == SessionState::kDisconnectInProgress);
  session->client_info.sm_req_ts = rdtsc();

  SmPkt sm_pkt;
  sm_pkt.pkt_type = session->state == SessionState::kConnectInProgress
                        ? SmPktType::kConnectReq
                        : SmPktType::kDisconnectReq;

  sm_pkt.err_type = SmErrType::kNoError;
  sm_pkt.uniq_token = session->uniq_token;
  sm_pkt.client = session->client;
  sm_pkt.server = session->server;
  sm_pkt_udp_tx_st(sm_pkt);
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

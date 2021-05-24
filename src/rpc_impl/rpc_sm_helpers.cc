/*
 * @file rpc_sm_helpers.cc
 * @brief Session management helper methods
 */
#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::handle_sm_rx_st() {
  assert(in_dispatch());
  MtQueue<SmWorkItem> &queue = nexus_hook_.sm_rx_queue_;

  while (queue.size_ > 0) {
    const SmWorkItem wi = queue.unlocked_pop();
    assert(!wi.is_reset());

    // Here, it's not a reset item, so we have a valid SM packet
    const SmPkt sm_pkt = wi.sm_pkt_;

    // If it's an SM response, remove pending requests for this session
    if (sm_pkt.is_resp() &&
        sm_pending_reqs_.count(sm_pkt.client_.session_num_) > 0) {
      sm_pending_reqs_.erase(
          sm_pending_reqs_.find(sm_pkt.client_.session_num_));
    }

    switch (wi.sm_pkt_.pkt_type_) {
      case SmPktType::kConnectReq: handle_connect_req_st(sm_pkt); break;
      case SmPktType::kDisconnectReq: handle_disconnect_req_st(sm_pkt); break;
      case SmPktType::kConnectResp: {
        handle_connect_resp_st(sm_pkt);
        break;
      }
      case SmPktType::kDisconnectResp: handle_disconnect_resp_st(sm_pkt); break;
      default: throw std::runtime_error("Invalid packet type");
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
    for (const SSlot &sslot : session->sslot_arr_) {
      free_msg_buffer(sslot.pre_resp_msgbuf_);  // Prealloc buf is always valid
    }
  }

  session_vec_.at(session->local_session_num_) = nullptr;
  delete session;  // This does nothing except free the session memory
}

template <class TTr>
void Rpc<TTr>::sm_pkt_udp_tx_st(const SmPkt &sm_pkt) {
  ERPC_INFO("Rpc %u: Sending packet %s.\n", rpc_id_,
            sm_pkt.to_string().c_str());
  const std::string rem_hostname =
      sm_pkt.is_req() ? sm_pkt.server_.hostname_ : sm_pkt.client_.hostname_;
  const uint16_t rem_sm_udp_port = sm_pkt.is_req()
                                       ? sm_pkt.server_.sm_udp_port_
                                       : sm_pkt.client_.sm_udp_port_;

  udp_client_.send(rem_hostname, rem_sm_udp_port, sm_pkt);
}

template <class TTr>
void Rpc<TTr>::send_sm_req_st(Session *session) {
  assert(in_dispatch());

  sm_pending_reqs_.insert(session->local_session_num_);  // Duplicates are fine
  session->client_info_.sm_req_ts_ = rdtsc();

  SmPkt sm_pkt;
  sm_pkt.pkt_type_ = session->state_ == SessionState::kConnectInProgress
                         ? SmPktType::kConnectReq
                         : SmPktType::kDisconnectReq;

  sm_pkt.err_type_ = SmErrType::kNoError;
  sm_pkt.uniq_token_ = session->uniq_token_;
  sm_pkt.client_ = session->client_;
  sm_pkt.server_ = session->server_;
  sm_pkt_udp_tx_st(sm_pkt);
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

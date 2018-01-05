#include "rpc.h"

namespace erpc {

// For both foreground and background request handlers, enqueue_response() may
// be called before or after the request handler returns to the event loop, at
// which point the event loop buries the request MsgBuffer.
//
// So sslot->rx_msgbuf may or may not be valid at this point.
template <class TTr>
void Rpc<TTr>::enqueue_response(ReqHandle *req_handle) {
  assert(req_handle != nullptr);

  // When called from a background thread, enqueue to the foreground thread
  if (unlikely(!in_dispatch())) {
    bg_queues.enqueue_response.unlocked_push(req_handle);
    return;
  }

  // If we're here, we're in the dispatch thread
  SSlot *sslot = static_cast<SSlot *>(req_handle);
  bury_req_msgbuf_server_st(sslot);  // Bury the possibly-dynamic req MsgBuffer

  Session *session = sslot->session;
  assert(session != nullptr && session->is_server());

  if (unlikely(!session->is_connected())) {
    // A session reset could be waiting for this enqueue_response()
    assert(session->state == SessionState::kResetInProgress);

    LOG_WARN(
        "eRPC Rpc %u: enqueue_response() for reset-in-progress session %u.\n",
        rpc_id, session->local_session_num);

    // Mark enqueue_response() as completed
    assert(sslot->server_info.req_type != kInvalidReqType);
    sslot->server_info.req_type = kInvalidReqType;

    return;  // During session reset, don't add packets to TX burst
  }

  MsgBuffer *resp_msgbuf =
      sslot->prealloc_used ? &sslot->pre_resp_msgbuf : &sslot->dyn_resp_msgbuf;
  assert(resp_msgbuf->is_valid_dynamic() && resp_msgbuf->data_size > 0);

  // Fill in packet 0's header
  pkthdr_t *resp_pkthdr_0 = resp_msgbuf->get_pkthdr_0();
  resp_pkthdr_0->req_type = sslot->server_info.req_type;
  resp_pkthdr_0->msg_size = resp_msgbuf->data_size;
  resp_pkthdr_0->dest_session_num = session->remote_session_num;
  resp_pkthdr_0->pkt_type = kPktTypeResp;
  resp_pkthdr_0->pkt_num = 0;
  resp_pkthdr_0->req_num = sslot->cur_req_num;

  // Fill in non-zeroth packet headers, if any
  if (resp_msgbuf->num_pkts > 1) {
    // Headers for non-zeroth packets are created by copying the 0th header, and
    // changing only the required fields.
    for (size_t i = 1; i < resp_msgbuf->num_pkts; i++) {
      pkthdr_t *resp_pkthdr_i = resp_msgbuf->get_pkthdr_n(i);
      *resp_pkthdr_i = *resp_pkthdr_0;
      resp_pkthdr_i->pkt_num = i;
    }
  }

  // Fill in the slot and reset queueing progress
  assert(sslot->tx_msgbuf == nullptr);  // Buried before calling request handler
  sslot->tx_msgbuf = resp_msgbuf;       // Mark response as valid
  sslot->server_info.rfr_rcvd = 0;

  // Mark enqueue_response() as completed
  assert(sslot->server_info.req_type != kInvalidReqType);
  sslot->server_info.req_type = kInvalidReqType;

  // Enqueue the zeroth response packet
  enqueue_pkt_tx_burst_st(
      sslot, 0, std::min(resp_msgbuf->data_size, TTr::kMaxDataPerPkt));
}

}  // End erpc

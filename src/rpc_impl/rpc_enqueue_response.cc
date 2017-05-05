#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::enqueue_response(ReqHandle *req_handle) {
  assert(req_handle != nullptr);
  SSlot *sslot = static_cast<SSlot *>(req_handle);

  // When called from a background thread, enqueue to the foreground thread
  if (small_rpc_unlikely(!in_creator())) {
    assert(sslot->server_info.req_func_type == ReqFuncType::kBackground);
    bg_queues.enqueue_response.unlocked_push_back(req_handle);
    return;
  }

  // If we're here, we are in the foreground thread
  assert(in_creator());

  Session *session = sslot->session;
  assert(session != nullptr);

  assert(session->is_server());
  assert(session->is_connected());

  // Foreground-terminal request handlers must call enqueue_response() before
  // returning to the event loop, which then buries the request MsgBuffers.
  // For these handlers only, rx_msgbuf must be valid at this point.
  ReqFuncType req_func_type = sslot->server_info.req_func_type;
  if (req_func_type == ReqFuncType::kFgTerminal) {
    // rx_msgbuf could be fake
    assert(sslot->rx_msgbuf.buf != nullptr && sslot->rx_msgbuf.check_magic());
  }

  MsgBuffer *resp_msgbuf;
  if (small_rpc_likely(sslot->prealloc_used)) {
    resp_msgbuf = &sslot->pre_resp_msgbuf;
  } else {
    resp_msgbuf = &sslot->dyn_resp_msgbuf;
  }

  // Sanity-check resp_msgbuf
  assert(resp_msgbuf->buf != nullptr && resp_msgbuf->check_magic());
  assert(resp_msgbuf->data_size > 0);

  // Fill in packet 0's header
  pkthdr_t *resp_pkthdr_0 = resp_msgbuf->get_pkthdr_0();
  resp_pkthdr_0->req_type = sslot->server_info.req_type;
  resp_pkthdr_0->msg_size = resp_msgbuf->data_size;
  resp_pkthdr_0->dest_session_num = session->remote_session_num;
  resp_pkthdr_0->pkt_type = kPktTypeResp;
  resp_pkthdr_0->pkt_num = 0;
  resp_pkthdr_0->req_num = sslot->server_info.req_num;
  assert(resp_pkthdr_0->check_magic());

  // Fill in non-zeroth packet headers, if any
  if (small_rpc_unlikely(resp_msgbuf->num_pkts > 1)) {
    // Headers for non-zeroth packets are created by copying the 0th header, and
    // changing only the required fields.
    for (size_t i = 1; i < resp_msgbuf->num_pkts; i++) {
      pkthdr_t *resp_pkthdr_i = resp_msgbuf->get_pkthdr_n(i);
      *resp_pkthdr_i = *resp_pkthdr_0;
      resp_pkthdr_i->pkt_num = i;
    }
  }

  // Fill in the slot and reset queueing progress
  sslot->tx_msgbuf = resp_msgbuf;  // Valid response
  if (optlevel_large_rpc_supported) sslot->server_info.rfr_rcvd = 0;

  // Enqueue the first response packet
  size_t data_bytes = std::min(resp_msgbuf->data_size, TTr::kMaxDataPerPkt);
  enqueue_pkt_tx_burst_st(sslot, 0, data_bytes);
}

}  // End ERpc

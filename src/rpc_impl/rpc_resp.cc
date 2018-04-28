#include "rpc.h"

namespace erpc {

// For both foreground and background request handlers, enqueue_response() may
// be called before or after the request handler returns to the event loop, at
// which point the event loop buries the request MsgBuffer.
//
// So sslot->rx_msgbuf may or may not be valid at this point.
template <class TTr>
void Rpc<TTr>::enqueue_response(ReqHandle *req_handle) {
  // When called from a background thread, enqueue to the foreground thread
  if (unlikely(!in_dispatch())) {
    bg_queues.enqueue_response.unlocked_push(req_handle);
    return;
  }

  // If we're here, we're in the dispatch thread
  SSlot *sslot = static_cast<SSlot *>(req_handle);
  sslot->server_info.sav_num_req_pkts = sslot->server_info.req_msgbuf.num_pkts;
  bury_req_msgbuf_server_st(sslot);  // Bury the possibly-dynamic req MsgBuffer

  Session *session = sslot->session;
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

  // Fill in packet 0's header
  pkthdr_t *resp_pkthdr_0 = resp_msgbuf->get_pkthdr_0();
  resp_pkthdr_0->req_type = sslot->server_info.req_type;
  resp_pkthdr_0->msg_size = resp_msgbuf->data_size;
  resp_pkthdr_0->dest_session_num = session->remote_session_num;
  resp_pkthdr_0->pkt_type = kPktTypeResp;
  resp_pkthdr_0->pkt_num = sslot->server_info.sav_num_req_pkts - 1;
  resp_pkthdr_0->req_num = sslot->cur_req_num;

  // Fill in non-zeroth packet headers, if any
  if (resp_msgbuf->num_pkts > 1) {
    // Headers for non-zeroth packets are created by copying the 0th header, and
    // changing only the required fields.
    for (size_t i = 1; i < resp_msgbuf->num_pkts; i++) {
      pkthdr_t *resp_pkthdr_i = resp_msgbuf->get_pkthdr_n(i);
      *resp_pkthdr_i = *resp_pkthdr_0;
      resp_pkthdr_i->pkt_num = resp_pkthdr_0->pkt_num + i;
    }
  }

  // Fill in the slot and reset queueing progress
  assert(sslot->tx_msgbuf == nullptr);  // Buried before calling request handler
  sslot->tx_msgbuf = resp_msgbuf;       // Mark response as valid

  // Mark enqueue_response() as completed
  assert(sslot->server_info.req_type != kInvalidReqType);
  sslot->server_info.req_type = kInvalidReqType;

  enqueue_pkt_tx_burst_st(sslot, 0, nullptr);  // 0 = packet index, not pkt_num
}

template <class TTr>
void Rpc<TTr>::process_resp_one_st(SSlot *sslot, const pkthdr_t *pkthdr,
                                   size_t rx_tsc) {
  assert(in_dispatch());
  assert(pkthdr->req_num <= sslot->cur_req_num);  // Response from the future?

  // Handle reordering
  if (unlikely(!in_order_client(sslot, pkthdr))) {
    LOG_REORDER(trace_file,
                "eRPC Rpc %u: Received out-of-order response for session %u. "
                "Req/pkt numbers: %zu/%zu (pkt), %zu/%zu (sslot). Dropping.\n",
                rpc_id, sslot->session->local_session_num, pkthdr->req_num,
                pkthdr->pkt_num, sslot->cur_req_num, sslot->client_info.num_rx);
    return;
  }

  auto &ci = sslot->client_info;
  MsgBuffer *resp_msgbuf = ci.resp_msgbuf;

  if (kCcRateComp) update_timely_rate(sslot, pkthdr->pkt_num, rx_tsc);
  bump_credits(sslot->session);
  ci.num_rx++;

  // Special handling for single-packet respones
  if (likely(pkthdr->msg_size <= TTr::kMaxDataPerPkt)) {
    resize_msg_buffer(resp_msgbuf, pkthdr->msg_size);

    // Copy eRPC header and data, but not Transport headroom
    memcpy(resp_msgbuf->get_pkthdr_0()->ehdrptr(), pkthdr->ehdrptr(),
           pkthdr->msg_size + sizeof(pkthdr_t) - kHeadroom);

    // Fall through to invoke continuation
  } else {
    ci.progress_tsc = ev_loop_tsc;

    // This is an in-order response packet. So, we still have the request.
    MsgBuffer *req_msgbuf = sslot->tx_msgbuf;

    if (pkthdr->pkt_num == req_msgbuf->num_pkts - 1) {
      // This is the first response packet. Resize and copy eRPC header.
      resize_msg_buffer(resp_msgbuf, pkthdr->msg_size);
      memcpy(resp_msgbuf->get_pkthdr_0()->ehdrptr(), pkthdr->ehdrptr(),
             sizeof(pkthdr_t) - kHeadroom);
    }

    // Transmit remaining RFRs before response memcpy. We have credits.
    if (ci.num_tx != wire_pkts(req_msgbuf, resp_msgbuf)) kick_rfr_st(sslot);

    // Hdr 0 was copied earlier, other headers are unneeded, so copy just data.
    const size_t pkt_idx = resp_ntoi(pkthdr->pkt_num, resp_msgbuf->num_pkts);
    copy_data_to_msgbuf(resp_msgbuf, pkt_idx, pkthdr);

    if (ci.num_rx != wire_pkts(req_msgbuf, resp_msgbuf)) return;
    // Fall through to invoke continuation
  }

  // If we are here, the complete response has been received
  sslot->tx_msgbuf = nullptr;  // Mark response as received
  if (ci.cont_etid == kInvalidBgETid) {
    ci.cont_func(static_cast<RespHandle *>(sslot), context, ci.tag);
  } else {
    submit_background_st(sslot, Nexus::BgWorkItemType::kResp, ci.cont_etid);
  }
  return;
}

FORCE_COMPILE_TRANSPORTS

}  // End erpc

#include "rpc.h"

namespace erpc {

// For both foreground and background request handlers, enqueue_response() may
// be called before or after the request handler returns to the event loop, at
// which point the event loop buries the request MsgBuffer.
//
// So sslot->rx_msgbuf may or may not be valid at this point.
template <class TTr>
void Rpc<TTr>::enqueue_response(ReqHandle *req_handle, MsgBuffer *resp_msgbuf) {
  // When called from a background thread, enqueue to the foreground thread
  if (unlikely(!in_dispatch())) {
    bg_queues_.enqueue_response_.unlocked_push(
        enq_resp_args_t(req_handle, resp_msgbuf));
    return;
  }

  // If we're here, we're in the dispatch thread
  SSlot *sslot = static_cast<SSlot *>(req_handle);
  sslot->server_info_.sav_num_req_pkts_ =
      sslot->server_info_.req_msgbuf_.num_pkts_;
  bury_req_msgbuf_server_st(sslot);  // Bury the possibly-dynamic req MsgBuffer

  Session *session = sslot->session_;
  if (unlikely(!session->is_connected())) {
    // A session reset could be waiting for this enqueue_response()
    assert(session->state_ == SessionState::kResetInProgress);

    ERPC_WARN("Rpc %u, lsn %u: enqueue_response() while reset in progress.\n",
              rpc_id_, session->local_session_num_);

    // Mark enqueue_response() as completed
    assert(sslot->server_info_.req_type_ != kInvalidReqType);
    sslot->server_info_.req_type_ = kInvalidReqType;

    return;  // During session reset, don't add packets to TX burst
  }

  // Fill in packet 0's header
  pkthdr_t *resp_pkthdr_0 = resp_msgbuf->get_pkthdr_0();
  resp_pkthdr_0->req_type_ = sslot->server_info_.req_type_;
  resp_pkthdr_0->msg_size_ = resp_msgbuf->data_size_;
  resp_pkthdr_0->dest_session_num_ = session->remote_session_num_;
  resp_pkthdr_0->pkt_type_ = PktType::kResp;
  resp_pkthdr_0->pkt_num_ = sslot->server_info_.sav_num_req_pkts_ - 1;
  resp_pkthdr_0->req_num_ = sslot->cur_req_num_;

  // Fill in non-zeroth packet headers, if any
  if (resp_msgbuf->num_pkts_ > 1) {
    // Headers for non-zeroth packets are created by copying the 0th header, and
    // changing only the required fields.
    for (size_t i = 1; i < resp_msgbuf->num_pkts_; i++) {
      pkthdr_t *resp_pkthdr_i = resp_msgbuf->get_pkthdr_n(i);
      *resp_pkthdr_i = *resp_pkthdr_0;
      resp_pkthdr_i->pkt_num_ = resp_pkthdr_0->pkt_num_ + i;
    }
  }

  // Fill in the slot and reset queueing progress
  assert(sslot->tx_msgbuf_ ==
         nullptr);                  // Buried before calling request handler
  sslot->tx_msgbuf_ = resp_msgbuf;  // Mark response as valid

  // Mark enqueue_response() as completed
  assert(sslot->server_info_.req_type_ != kInvalidReqType);
  sslot->server_info_.req_type_ = kInvalidReqType;

  enqueue_pkt_tx_burst_st(sslot, 0, nullptr);  // 0 = packet index, not pkt_num
}

template <class TTr>
void Rpc<TTr>::process_resp_one_st(SSlot *sslot, const pkthdr_t *pkthdr,
                                   size_t rx_tsc) {
  assert(in_dispatch());
  assert(pkthdr->req_num_ <= sslot->cur_req_num_);

  // Handle reordering
  if (unlikely(!in_order_client(sslot, pkthdr))) {
    ERPC_REORDER(
        "Rpc %u, lsn %u (%s): Received out-of-order response. "
        "Packet %zu/%zu, sslot %zu/%s. Dropping.\n",
        rpc_id_, sslot->session_->local_session_num_,
        sslot->session_->get_remote_hostname().c_str(), pkthdr->req_num_,
        pkthdr->pkt_num_, sslot->cur_req_num_, sslot->progress_str().c_str());
    return;
  }

  auto &ci = sslot->client_info_;
  MsgBuffer *resp_msgbuf = ci.resp_msgbuf_;

  // Update client tracking metadata
  if (kCcRateComp) update_timely_rate(sslot, pkthdr->pkt_num_, rx_tsc);
  bump_credits(sslot->session_);
  ci.num_rx_++;
  ci.progress_tsc_ = ev_loop_tsc_;

  // Special handling for single-packet responses
  if (likely(pkthdr->msg_size_ <= TTr::kMaxDataPerPkt)) {
    resize_msg_buffer(resp_msgbuf, pkthdr->msg_size_);

    // Copy eRPC header and data (but not Transport headroom). The eRPC header
    // will be needed (e.g., to determine the request type) if the continuation
    // runs in a background thread.
    memcpy(resp_msgbuf->get_pkthdr_0()->ehdrptr(), pkthdr->ehdrptr(),
           pkthdr->msg_size_ + sizeof(pkthdr_t) - kHeadroom);

    // Fall through to invoke continuation
  } else {
    // This is an in-order response packet. So, we still have the request.
    MsgBuffer *req_msgbuf = sslot->tx_msgbuf_;

    if (pkthdr->pkt_num_ == req_msgbuf->num_pkts_ - 1) {
      // This is the first response packet. Size the response and copy header.
      resize_msg_buffer(resp_msgbuf, pkthdr->msg_size_);
      memcpy(resp_msgbuf->get_pkthdr_0()->ehdrptr(), pkthdr->ehdrptr(),
             sizeof(pkthdr_t) - kHeadroom);
    }

    // Transmit remaining RFRs before response memcpy. We have credits.
    if (ci.num_tx_ != wire_pkts(req_msgbuf, resp_msgbuf)) kick_rfr_st(sslot);

    // Hdr 0 was copied earlier, other headers are unneeded, so copy just data.
    const size_t pkt_idx = resp_ntoi(pkthdr->pkt_num_, req_msgbuf->num_pkts_);
    copy_data_to_msgbuf(resp_msgbuf, pkt_idx, pkthdr);

    if (ci.num_rx_ != wire_pkts(req_msgbuf, resp_msgbuf)) return;
    // Else fall through to invoke continuation
  }

  // Here, the complete response has been received. All references to sslot must
  // have been removed previously, before invalidating the sslot (done next).
  // 1. The TX batch or DMA queue cannot contain a reference because we drain
  //    it after retransmission.
  // 2. The wheel cannot contain a reference because we (a) wait for sslot to
  //    drain from the wheel before retransmitting, and (b) discard spurious
  //    corresponding packets received for packets in the wheel.
  assert(ci.wheel_count_ == 0);

  sslot->tx_msgbuf_ = nullptr;  // Mark response as received
  delete_from_active_rpc_list(*sslot);

  // Free-up this sslot by copying-out needed fields. The sslot may get re-used
  // immediately if there are backlogged requests, or much later from a request
  // enqueued by a background thread.
  const erpc_cont_func_t cont_func = ci.cont_func_;
  void *tag = ci.tag_;
  const size_t cont_etid = ci.cont_etid_;

  Session *session = sslot->session_;
  session->client_info_.sslot_free_vec_.push_back(sslot->index_);

  // Clear up one request from the backlog if needed
  if (!session->client_info_.enq_req_backlog_.empty()) {
    // We just got a new sslot, and we should have no more if there's backlog
    assert(session->client_info_.sslot_free_vec_.size() == 1);
    enq_req_args_t &args = session->client_info_.enq_req_backlog_.front();
    enqueue_request(args.session_num_, args.req_type_, args.req_msgbuf_,
                    args.resp_msgbuf_, args.cont_func_, args.tag_,
                    args.cont_etid_);
    session->client_info_.enq_req_backlog_.pop();
  }

  if (likely(cont_etid == kInvalidBgETid)) {
    cont_func(context_, tag);
  } else {
    submit_bg_resp_st(cont_func, tag, cont_etid);
  }
  return;
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

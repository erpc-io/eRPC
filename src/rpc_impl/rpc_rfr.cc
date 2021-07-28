#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::enqueue_rfr_st(SSlot *sslot, const pkthdr_t *resp_pkthdr) {
  assert(in_dispatch());

  MsgBuffer *ctrl_msgbuf = &ctrl_msgbufs_[ctrl_msgbuf_head_];
  ctrl_msgbuf_head_++;
  if (ctrl_msgbuf_head_ == 2 * TTr::kUnsigBatch) ctrl_msgbuf_head_ = 0;

  // Fill in the RFR packet header. Avoid copying resp_pkthdr's headroom.
  pkthdr_t *rfr_pkthdr = ctrl_msgbuf->get_pkthdr_0();
  rfr_pkthdr->req_type_ = resp_pkthdr->req_type_;
  rfr_pkthdr->msg_size_ = 0;
  rfr_pkthdr->dest_session_num_ = sslot->session_->remote_session_num_;
  rfr_pkthdr->pkt_type_ = PktType::kRFR;
  rfr_pkthdr->pkt_num_ = sslot->client_info_.num_tx_;
  rfr_pkthdr->req_num_ = resp_pkthdr->req_num_;
  rfr_pkthdr->magic_ = kPktHdrMagic;

  enqueue_hdr_tx_burst_st(
      sslot, ctrl_msgbuf,
      &sslot->client_info_.tx_ts_[rfr_pkthdr->pkt_num_ % kSessionCredits]);
}

template <class TTr>
void Rpc<TTr>::process_rfr_st(SSlot *sslot, const pkthdr_t *pkthdr) {
  assert(in_dispatch());
  assert(!sslot->is_client_);
  auto &si = sslot->server_info_;

  // Handle reordering. If request numbers match, then we have not reset num_rx.
  assert(pkthdr->req_num_ <= sslot->cur_req_num_);
  bool in_order = (pkthdr->req_num_ == sslot->cur_req_num_) &&
                  (pkthdr->pkt_num_ == si.num_rx_);
  if (unlikely(!in_order)) {
    char issue_msg[kMaxIssueMsgLen];
    // The static_cast for pkt_num_ is a hack for compiling with clang
    sprintf(issue_msg,
            "Rpc %u, lsn %u (%s): Received out-of-order RFR. "
            "Pkt = %zu/%zu. cur_req_num = %zu, num_rx = %zu. Action",
            rpc_id_, sslot->session_->local_session_num_,
            sslot->session_->get_remote_hostname().c_str(), pkthdr->req_num_,
            static_cast<size_t>(pkthdr->pkt_num_), sslot->cur_req_num_,
            si.num_rx_);

    if (pkthdr->req_num_ < sslot->cur_req_num_ ||
        pkthdr->pkt_num_ > si.num_rx_) {
      // Reject RFR for old requests or future packets in this request
      ERPC_REORDER("%s: Dropping.\n", issue_msg);
      return;
    }

    // If we're here, this is a past RFR packet for this request. So, we still
    // have the response, and we saved request packet count.
    ERPC_REORDER("%s: Re-sending response.\n", issue_msg);
    enqueue_pkt_tx_burst_st(
        sslot, resp_ntoi(pkthdr->pkt_num_, si.sav_num_req_pkts_), nullptr);
    drain_tx_batch_and_dma_queue();
    return;
  }

  sslot->server_info_.num_rx_++;
  enqueue_pkt_tx_burst_st(
      sslot, resp_ntoi(pkthdr->pkt_num_, si.sav_num_req_pkts_), nullptr);
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

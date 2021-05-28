#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::enqueue_cr_st(SSlot *sslot, const pkthdr_t *req_pkthdr) {
  assert(in_dispatch());

  MsgBuffer *ctrl_msgbuf = &ctrl_msgbufs_[ctrl_msgbuf_head_];
  ctrl_msgbuf_head_++;
  if (ctrl_msgbuf_head_ == 2 * TTr::kUnsigBatch) ctrl_msgbuf_head_ = 0;

  // Fill in the CR packet header. Avoid copying req_pkthdr's headroom.
  pkthdr_t *cr_pkthdr = ctrl_msgbuf->get_pkthdr_0();
  cr_pkthdr->req_type_ = req_pkthdr->req_type_;
  cr_pkthdr->msg_size_ = 0;
  cr_pkthdr->dest_session_num_ = sslot->session_->remote_session_num_;
  cr_pkthdr->pkt_type_ = PktType::kExplCR;
  cr_pkthdr->pkt_num_ = req_pkthdr->pkt_num_;
  cr_pkthdr->req_num_ = req_pkthdr->req_num_;
  cr_pkthdr->magic_ = kPktHdrMagic;

  enqueue_hdr_tx_burst_st(sslot, ctrl_msgbuf, nullptr);
}

template <class TTr>
void Rpc<TTr>::process_expl_cr_st(SSlot *sslot, const pkthdr_t *pkthdr,
                                  size_t rx_tsc) {
  assert(in_dispatch());
  assert(pkthdr->req_num_ <= sslot->cur_req_num_);

  // Handle reordering
  if (unlikely(!in_order_client(sslot, pkthdr))) {
    ERPC_REORDER(
        "Rpc %u, lsn %u (%s): Received out-of-order CR. "
        "Packet %zu/%zu, sslot: %zu/%s. Dropping.\n",
        rpc_id_, sslot->session_->local_session_num_,
        sslot->session_->get_remote_hostname().c_str(), pkthdr->req_num_,
        pkthdr->pkt_num_, sslot->cur_req_num_, sslot->progress_str().c_str());
    return;
  }

  // Update client tracking metadata
  if (kCcRateComp) update_timely_rate(sslot, pkthdr->pkt_num_, rx_tsc);
  bump_credits(sslot->session_);
  sslot->client_info_.num_rx_++;
  sslot->client_info_.progress_tsc_ = ev_loop_tsc_;

  // If we've transmitted all request pkts, there's nothing more to TX yet
  if (req_pkts_pending(sslot)) kick_req_st(sslot);  // credits >= 1
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

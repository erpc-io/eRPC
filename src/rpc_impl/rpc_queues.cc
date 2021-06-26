#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::process_credit_stall_queue_st() {
  assert(in_dispatch());
  size_t write_index = 0;  // Re-add incomplete sslots at this index

  for (SSlot *sslot : stallq_) {
    if (sslot->session_->client_info_.credits_ > 0) {
      // sslots in stall queue have packets to send
      req_pkts_pending(sslot) ? kick_req_st(sslot) : kick_rfr_st(sslot);
    } else {
      stallq_[write_index++] = sslot;
    }
  }

  stallq_.resize(write_index);  // Number of sslots left = write_index
}

template <class TTr>
void Rpc<TTr>::process_wheel_st() {
  assert(in_dispatch());
  size_t cur_tsc = dpath_rdtsc();
  wheel_->reap(cur_tsc);

  size_t num_ready = wheel_->ready_queue_.size();
  for (size_t i = 0; i < num_ready; i++) {
    // kick_req/rfr() cannot be used here. This packet has already used a credit
    // and gone through the wheel; the kick logic might repeat these.
    wheel_ent_t &ent = wheel_->ready_queue_.front();
    auto *sslot = reinterpret_cast<SSlot *>(ent.sslot_);
    size_t pkt_num = ent.pkt_num_;
    size_t crd_i = pkt_num % kSessionCredits;

    ERPC_CC("Rpc %u: lsn/req/pkt %u,%zu/%zu, reaped at %.3f us.\n", rpc_id_,
            sslot->session_->local_session_num_, sslot->cur_req_num_, pkt_num,
            to_usec(cur_tsc - creation_tsc_, freq_ghz_));

    auto &ci = sslot->client_info_;
    if (pkt_num < sslot->tx_msgbuf_->num_pkts_) {
      enqueue_pkt_tx_burst_st(sslot, pkt_num /* pkt_idx */, &ci.tx_ts_[crd_i]);
    } else {
      MsgBuffer *resp_msgbuf = ci.resp_msgbuf_;
      enqueue_rfr_st(sslot, resp_msgbuf->get_pkthdr_0());
    }

    sslot->client_info_.wheel_count_--;
    sslot->client_info_.in_wheel_[crd_i] = false;
    wheel_->ready_queue_.pop();
  }
}

template <class TTr>
void Rpc<TTr>::process_bg_queues_enqueue_request_st() {
  assert(in_dispatch());
  auto &queue = bg_queues_.enqueue_request_;
  const size_t cmds_to_process = queue.size_;  // Reduce cache line traffic

  for (size_t i = 0; i < cmds_to_process; i++) {
    enq_req_args_t args = queue.unlocked_pop();
    enqueue_request(args.session_num_, args.req_type_, args.req_msgbuf_,
                    args.resp_msgbuf_, args.cont_func_, args.tag_,
                    args.cont_etid_);
  }
}

template <class TTr>
void Rpc<TTr>::process_bg_queues_enqueue_response_st() {
  assert(in_dispatch());
  auto &queue = bg_queues_.enqueue_response_;
  const size_t cmds_to_process = queue.size_;  // Reduce cache line traffic

  for (size_t i = 0; i < cmds_to_process; i++) {
    enq_resp_args_t enq_resp_args = queue.unlocked_pop();
    enqueue_response(enq_resp_args.req_handle_, enq_resp_args.resp_msgbuf_);
  }
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

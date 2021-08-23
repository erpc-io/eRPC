#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::process_comps_st() {
  assert(in_dispatch());
  const size_t num_pkts = transport_->rx_burst();
  if (num_pkts == 0) return;

  // Measure RX burst size
  dpath_stat_inc(dpath_stats_.rx_burst_calls_, 1);
  dpath_stat_inc(dpath_stats_.pkts_rx_, num_pkts);

  // ev_loop_tsc was taken just before calling the packet RX code
  const size_t &batch_rx_tsc = ev_loop_tsc_;

  for (size_t i = 0; i < num_pkts; i++) {
    auto *pkthdr = reinterpret_cast<pkthdr_t *>(rx_ring_[rx_ring_head_]);
    rx_ring_head_ = (rx_ring_head_ + 1) % Transport::kNumRxRingEntries;

    // XXX: This acts as a stopgap function to filter non-eRPC packets, like
    // broadcast/ARP packets.
    if (unlikely(!pkthdr->check_magic())) {
      ERPC_INFO(
          "Rpc %u: Received %s with invalid magic. Packet headroom = %s. "
          "Dropping.\n",
          rpc_id_, pkthdr->to_string().c_str(),
          pkthdr->headroom_string().c_str());
      continue;
    }

    assert(pkthdr->msg_size_ <= kMaxMsgSize);  // msg_size can be 0 here

    if (unlikely(pkthdr->dest_session_num_ >= session_vec_.size())) {
      ERPC_WARN(
          "Rpc %u: Received %s for a session yet to be connected. Dropping.\n",
          rpc_id_, pkthdr->to_string().c_str());
      continue;
    }

    Session *session = session_vec_[pkthdr->dest_session_num_];
    if (unlikely(session == nullptr)) {
      ERPC_WARN("Rpc %u: Received %s for buried session. Dropping.\n", rpc_id_,
                pkthdr->to_string().c_str());
      continue;
    }

    if (unlikely(!session->is_connected())) {
      ERPC_WARN(
          "Rpc %u: Received %s for unconnected session (state %s). Dropping.\n",
          rpc_id_, pkthdr->to_string().c_str(),
          session_state_str(session->state_).c_str());
      continue;
    }

    // If we are here, we have a valid packet for a connected session
    ERPC_TRACE(
        "Rpc %u, lsn %u (%s): RX %s.\n", rpc_id_, session->local_session_num_,
        session->get_remote_hostname().c_str(), pkthdr->to_string().c_str());

    const size_t sslot_i = pkthdr->req_num_ % kSessionReqWindow;  // Bit shift
    SSlot *sslot = &session->sslot_arr_[sslot_i];

    switch (pkthdr->pkt_type_) {
      case PktType::kReq:
        pkthdr->msg_size_ <= TTr::kMaxDataPerPkt
            ? process_small_req_st(sslot, pkthdr)
            : process_large_req_one_st(sslot, pkthdr);
        break;
      case PktType::kResp: {
        size_t rx_tsc = kCcOptBatchTsc ? batch_rx_tsc : dpath_rdtsc();
        process_resp_one_st(sslot, pkthdr, rx_tsc);
        break;
      }
      case PktType::kRFR: {
        process_rfr_st(sslot, pkthdr);
        break;
      }
      case PktType::kExplCR: {
        size_t rx_tsc = kCcOptBatchTsc ? batch_rx_tsc : dpath_rdtsc();
        process_expl_cr_st(sslot, pkthdr, rx_tsc);
        break;
      }
    }
  }

  // Technically, these RECVs can be posted immediately after rx_burst(), or
  // even in the rx_burst() code.
  transport_->post_recvs(num_pkts);
}

template <class TTr>
void Rpc<TTr>::submit_bg_req_st(SSlot *sslot) {
  assert(in_dispatch());
  assert(nexus_->num_bg_threads_ > 0);

  const size_t bg_etid = fast_rand_.next_u32() % nexus_->num_bg_threads_;
  auto *req_queue = nexus_hook_.bg_req_queue_arr_[bg_etid];

  req_queue->unlocked_push(Nexus::BgWorkItem::make_req_item(context_, sslot));
}

template <class TTr>
void Rpc<TTr>::submit_bg_resp_st(erpc_cont_func_t cont_func, void *tag,
                                 size_t bg_etid) {
  assert(in_dispatch());
  assert(nexus_->num_bg_threads_ > 0);
  assert(bg_etid < nexus_->num_bg_threads_);

  auto *req_queue = nexus_hook_.bg_req_queue_arr_[bg_etid];
  req_queue->unlocked_push(
      Nexus::BgWorkItem::make_resp_item(context_, cont_func, tag));
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

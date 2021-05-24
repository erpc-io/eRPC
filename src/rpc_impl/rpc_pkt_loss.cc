/*
 * @file rpc_pkt_loss.cc
 * @brief Packet loss handling functions
 */
#include "rpc.h"

namespace erpc {

// This handles both datapath and management packet loss. This is called
// rarely, so no need to optimize heavily.
template <class TTr>
void Rpc<TTr>::pkt_loss_scan_st() {
  assert(in_dispatch());

  // Datapath packet loss
  SSlot *cur = active_rpcs_root_sentinel_.client_info_.next_;  // The iterator
  while (cur != &active_rpcs_tail_sentinel_) {
    // Don't re-tx or check for server failure if we're just stalled on credits
    if (cur->client_info_.num_tx_ == cur->client_info_.num_rx_) {
      cur = cur->client_info_.next_;
      continue;
    }

    // Check if the server has failed. XXX.
    if (false) {
      // We cannot destroy this session while it still has packets in the
      // wheel. We will try again in the next epoch.
      bool session_pkts_in_wheel = false;
      for (const SSlot &s : cur->session_->sslot_arr_) {
        session_pkts_in_wheel |= (s.client_info_.wheel_count_ > 0);
      }

      if (session_pkts_in_wheel) {
        pkt_loss_stats_.still_in_wheel_during_retx_++;

        ERPC_REORDER(
            "Rpc %u, lsn %u: Could not reset because packets still in wheel.\n",
            rpc_id_, cur->session_->local_session_num_);
        continue;
      }

      // If we are here, we will destroy the session
      ERPC_INFO("Rpc %u, lsn %u: Server disconnected. Destroying session.\n",
                rpc_id_, cur->session_->local_session_num_);
      drain_tx_batch_and_dma_queue();

      // In this case, we will delete sslots of this session from the active RPC
      // list, including the current slot. Re-start the scan to handle this.
      handle_reset_client_st(cur->session_);
      cur = active_rpcs_root_sentinel_.client_info_.next_;
      continue;
    }

    // If the server hasn't failed, check for packet loss
    if (ev_loop_tsc_ - cur->client_info_.progress_tsc_ > rpc_rto_cycles_) {
      pkt_loss_retransmit_st(cur);
      drain_tx_batch_and_dma_queue();
    }

    cur = cur->client_info_.next_;
  }

  // Management packet loss
  for (uint16_t session_num : sm_pending_reqs_) {
    Session *session = session_vec_[session_num];
    if (session == nullptr) continue;  // XXX: Can this happen?

    switch (session->state_) {
      case SessionState::kConnectInProgress:
      case SessionState::kDisconnectInProgress: {
        // Session management packet loss detection
        const size_t ms_elapsed =
            to_msec(rdtsc() - session->client_info_.sm_req_ts_, freq_ghz_);
        if (ms_elapsed > kSMTimeoutMs) send_sm_req_st(session);
        break;
      }
      default: break;
    }
  }
}

template <class TTr>
void Rpc<TTr>::pkt_loss_retransmit_st(SSlot *sslot) {
  assert(in_dispatch());
  assert(sslot->tx_msgbuf_ != nullptr);  // sslot has a valid request

  auto &ci = sslot->client_info_;
  auto &credits = sslot->session_->client_info_.credits_;
  MsgBuffer *req_msgbuf = sslot->tx_msgbuf_;

  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg,
          "Rpc %u, lsn %u (%s): Pkt loss suspected for req %zu (%s). Action",
          rpc_id_, sslot->session_->local_session_num_,
          sslot->session_->get_remote_hostname().c_str(),
          req_msgbuf->get_pkthdr_0()->req_num_, sslot->progress_str().c_str());

  const size_t delta = ci.num_tx_ - ci.num_rx_;
  assert(credits + delta <= kSessionCredits);

  if (unlikely(delta == 0)) {
    ERPC_REORDER("%s: False positive. Ignoring.\n", issue_msg);
    return;
  }

  // We have num_tx > num_rx, so stallq cannot contain sslot
  assert(std::find(stallq_.begin(), stallq_.end(), sslot) == stallq_.end());

  // Do not roll back if this request still has packets in the wheel. Deleting
  // from the wheel is too complex.
  if (unlikely(sslot->client_info_.wheel_count_ > 0)) {
    pkt_loss_stats_.still_in_wheel_during_retx_++;
    ERPC_REORDER("%s: Packets still in wheel. Ignoring.\n", issue_msg);
    return;
  }

  // If we're here, we will roll back and retransmit
  pkt_loss_stats_.num_re_tx_++;
  sslot->session_->client_info_.num_re_tx_++;

  ERPC_REORDER("%s: Retransmitting %s.\n", issue_msg,
               ci.num_rx_ < req_msgbuf->num_pkts_ ? "requests" : "RFRs");
  credits += delta;
  ci.num_tx_ = ci.num_rx_;
  ci.progress_tsc_ = ev_loop_tsc_;

  req_pkts_pending(sslot) ? kick_req_st(sslot) : kick_rfr_st(sslot);
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

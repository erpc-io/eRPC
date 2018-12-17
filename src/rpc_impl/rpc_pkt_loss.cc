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
  SSlot *it = active_rpcs_root_sentinel.client_info.next;  // The iterator
  while (it != &active_rpcs_tail_sentinel) {
    // We might destroy *it below, so move the iterator first
    SSlot *sslot = it;
    it = it->client_info.next;

    // Don't re-tx or check for server failure if we're just stalled on credits
    if (sslot->client_info.num_tx == sslot->client_info.num_rx) continue;

    size_t cycles_elapsed = ev_loop_tsc - sslot->client_info.progress_tsc;

    if (to_msec(cycles_elapsed, freq_ghz) > kServerFailureTimeoutMs) {
      handle_reset_client_st(sslot->session);
    } else if (cycles_elapsed > rpc_rto_cycles) {
      // Don't retransmit if we initiated session reset
      pkt_loss_retransmit_st(sslot);
    }
  }

  // Management packet loss
  for (uint16_t session_num : sm_pending_reqs) {
    Session *session = session_vec[session_num];
    if (session == nullptr) continue;  // XXX: Can this happen?

    switch (session->state) {
      case SessionState::kConnectInProgress:
      case SessionState::kDisconnectInProgress: {
        // Session management packet loss detection
        const size_t ms_elapsed =
            to_msec(rdtsc() - session->client_info.sm_req_ts, freq_ghz);
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
  assert(sslot->tx_msgbuf != nullptr);  // sslot has a valid request

  auto &ci = sslot->client_info;
  auto &credits = sslot->session->client_info.credits;
  MsgBuffer *req_msgbuf = sslot->tx_msgbuf;

  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg,
          "Rpc %u, lsn %u (%s): Pkt loss suspected for req %zu (%s). Action",
          rpc_id, sslot->session->local_session_num,
          sslot->session->get_remote_hostname().c_str(),
          req_msgbuf->get_req_num(), sslot->progress_str().c_str());

  const size_t delta = ci.num_tx - ci.num_rx;
  assert(credits + delta <= kSessionCredits);

  if (unlikely(delta == 0)) {
    LOG_REORDER("%s: False positive. Ignoring.\n", issue_msg);
    return;
  }

  // We have num_tx > num_rx, so stallq cannot contain sslot
  assert(std::find(stallq.begin(), stallq.end(), sslot) == stallq.end());

  // Deleting from the rate limiter is too complex
  if (unlikely(sslot->client_info.wheel_count > 0)) {
    pkt_loss_stats.still_in_wheel_during_retx++;
    LOG_REORDER("%s: Packets still in wheel. Ignoring.\n", issue_msg);
    return;
  }

  // If we're here, we will roll back and retransmit
  pkt_loss_stats.num_re_tx++;
  sslot->session->client_info.num_re_tx++;

  LOG_REORDER("%s: Retransmitting %s.\n", issue_msg,
              ci.num_rx < req_msgbuf->num_pkts ? "requests" : "RFRs");
  credits += delta;
  ci.num_tx = ci.num_rx;
  ci.progress_tsc = ev_loop_tsc;

  req_pkts_pending(sslot) ? kick_req_st(sslot) : kick_rfr_st(sslot);
  drain_tx_batch_and_dma_queue();
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

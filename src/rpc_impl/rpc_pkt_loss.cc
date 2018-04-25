/*
 * @file rpc_pkt_loss.cc
 * @brief Packet loss handling functions
 */
#include "rpc.h"

namespace erpc {

// This handles both datapath and management packet loss
template <class TTr>
void Rpc<TTr>::pkt_loss_scan_st() {
  assert(in_dispatch());

  for (Session *session : session_vec) {
    // Process only client sessions
    if (session == nullptr || session->is_server()) continue;

    switch (session->state) {
      case SessionState::kConnected: {
        // Datapath packet loss detection
        for (SSlot &sslot : session->sslot_arr) {
          if (sslot.tx_msgbuf == nullptr) continue;     // Response received
          if (sslot.client_info.num_tx == 0) continue;  // No packet sent

          assert(sslot.tx_msgbuf->get_req_num() == sslot.cur_req_num);

          size_t cycles_elapsed = ev_loop_tsc - sslot.client_info.progress_tsc;
          if (cycles_elapsed > rpc_rto_cycles) pkt_loss_retransmit_st(&sslot);
        }

        break;
      }
      case SessionState::kConnectInProgress:
      case SessionState::kDisconnectInProgress: {
        // Session management packet loss detection
        const size_t ms_elapsed =
            to_msec(rdtsc() - session->client_info.sm_req_ts, nexus->freq_ghz);
        if (ms_elapsed > kSMTimeoutMs) send_sm_req_st(session);
        break;
      }
      case SessionState::kResetInProgress:
        break;
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
          "eRPC Rpc %u: Packet loss suspected for session %u, req %zu. "
          "num_tx %zu, num_rx %zu. Action",
          rpc_id, sslot->session->local_session_num, req_msgbuf->get_req_num(),
          ci.num_tx, ci.num_rx);

  const size_t delta = ci.num_tx - ci.num_rx;
  assert(credits + delta <= kSessionCredits);

  if (unlikely(delta == 0)) {
    // This can happen if:
    // (a) We're stalled on credits: credit stall queue will make progress.
    // (b) We have received the full response and a background thread currently
    // owns sslot. In this case, the bg thread cannot modify num_rx or num_tx.
    LOG_REORDER("%s: False positive. Ignoring.\n", issue_msg);
    return;
  }

  // If we're here, we will roll back and retransmit
  sslot->session->client_info.cc.num_retransmissions++;
  credits += delta;
  ci.num_tx = ci.num_rx;
  ci.progress_tsc = ev_loop_tsc;

  // sslot may be in dispatch queues, but not in background queues since we
  // don't have the full response.
  //
  // TODO: Remove sslot from timing wheel
  credit_stall_txq.erase(
      std::remove(credit_stall_txq.begin(), credit_stall_txq.end(), sslot),
      credit_stall_txq.end());

  LOG_REORDER("%s: Retransmitting %s.\n", issue_msg,
              ci.num_rx < req_msgbuf->num_pkts ? "request" : "rfr");
  req_pkts_pending(sslot) ? kick_req_st(sslot) : kick_rfr_st(sslot);
}

FORCE_COMPILE_TRANSPORTS

}  // End erpc

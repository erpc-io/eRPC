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

          size_t cycles_elapsed = rdtsc() - sslot.client_info.enqueue_req_tsc;
          size_t ms_elapsed = to_msec(cycles_elapsed, nexus->freq_ghz);
          if (ms_elapsed >= kRpcPktLossTimeoutMs) {
            pkt_loss_retransmit_st(&sslot);
          }
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

  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg,
          "eRPC Rpc %u: Packet loss suspected for session %u, req %zu. "
          "num_tx %zu, num_rx %zu. Action",
          rpc_id, sslot->session->local_session_num,
          sslot->tx_msgbuf->get_req_num(), ci.num_tx, ci.num_rx);

  const size_t delta = ci.num_tx - ci.num_rx;
  assert(credits + delta <= kSessionCredits);

  if (delta == 0) {
    // This can happen if we're stalled on credits. Or, we have received the
    // full response and a background thread currently owns sslot. In the latter
    // case, the background thread cannot modify num_rx or num_tx.
    LOG_REORDER("%s: False positive. Ignoring.\n", issue_msg);
  }

  credits += delta;
  ci.num_tx = ci.num_rx;

  if (ci.num_rx < sslot->tx_msgbuf->num_pkts) {
    // We haven't received the first response packet
    LOG_REORDER("%s: Retransmitting request.\n", issue_msg);

    // sslot may be in dispatch queues, but not in background queues since
    // we don't have the full response.
    credit_stall_txq.erase(
        std::remove(credit_stall_txq.begin(), credit_stall_txq.end(), sslot),
        credit_stall_txq.end());

    sslot->client_info.enqueue_req_tsc = pkt_loss_epoch_tsc;
    bool all_pkts_tx = req_sslot_tx_credits_cc_st(sslot);
    if (!all_pkts_tx) credit_stall_txq.push_back(sslot);
  } else {
    // We have received the first response packet, but we don't have the full
    // response (which must be multi-packet => dynamic). So, a continuation in
    // the background thread can't invalidate resp_msgbuf.
    MsgBuffer *resp_msgbuf = sslot->client_info.resp_msgbuf;
    assert(resp_msgbuf->is_dynamic_and_matches(sslot->tx_msgbuf));

    LOG_REORDER("%s: Retransmitting RFR.\n", issue_msg);
    sslot->client_info.enqueue_req_tsc = pkt_loss_epoch_tsc;
    enqueue_rfr_st(sslot, resp_msgbuf->get_pkthdr_0());
  }
}

FORCE_COMPILE_TRANSPORTS

}  // End erpc

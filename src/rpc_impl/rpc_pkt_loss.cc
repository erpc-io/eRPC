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
  SSlot *cur = active_rpcs_root_sentinel.client_info.next;  // The iterator
  while (cur != &active_rpcs_tail_sentinel) {
    // Don't re-tx or check for server failure if we're just stalled on credits
    if (cur->client_info.num_tx == cur->client_info.num_rx) {
      cur = cur->client_info.next;
      continue;
    }

    // Check if the server has failed. XXX.
    if (false) {
      // We cannot destroy this session while it still has packets in the
      // wheel. We will try again in the next epoch.
      bool session_pkts_in_wheel = false;
      for (const SSlot &s : cur->session->sslot_arr) {
        session_pkts_in_wheel |= (s.client_info.wheel_count > 0);
      }

      if (session_pkts_in_wheel) {
        pkt_loss_stats.still_in_wheel_during_retx++;

        ERPC_REORDER(
            "Rpc %u, lsn %u: Could not reset because packets still in wheel.\n",
            rpc_id, cur->session->local_session_num);
        continue;
      }

      // If we are here, we will destroy the session
      ERPC_INFO("Rpc %u, lsn %u: Server disconnected. Destroying session.\n",
                rpc_id, cur->session->local_session_num);
      drain_tx_batch_and_dma_queue();

      // In this case, we will delete sslots of this session from the active RPC
      // list, including the current slot. Re-start the scan to handle this.
      handle_reset_client_st(cur->session);
      cur = active_rpcs_root_sentinel.client_info.next;
      continue;
    }

    // If the server hasn't failed, check for packet loss
    if (ev_loop_tsc - cur->client_info.progress_tsc > rpc_rto_cycles) {
      pkt_loss_retransmit_st(cur);
      drain_tx_batch_and_dma_queue();
    }

    cur = cur->client_info.next;
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
          req_msgbuf->get_pkthdr_0()->req_num, sslot->progress_str().c_str());

  const size_t delta = ci.num_tx - ci.num_rx;
  assert(credits + delta <= kSessionCredits);

  if (unlikely(delta == 0)) {
    ERPC_REORDER("%s: False positive. Ignoring.\n", issue_msg);
    return;
  }

  // We have num_tx > num_rx, so stallq cannot contain sslot
  assert(std::find(stallq.begin(), stallq.end(), sslot) == stallq.end());

  // Do not roll back if this request still has packets in the wheel. Deleting
  // from the wheel is too complex.
  if (unlikely(sslot->client_info.wheel_count > 0)) {
    pkt_loss_stats.still_in_wheel_during_retx++;
    ERPC_REORDER("%s: Packets still in wheel. Ignoring.\n", issue_msg);
    return;
  }

  // If we're here, we will roll back and retransmit
  pkt_loss_stats.num_re_tx++;
  sslot->session->client_info.num_re_tx++;

  ERPC_REORDER("%s: Retransmitting %s.\n", issue_msg,
               ci.num_rx < req_msgbuf->num_pkts ? "requests" : "RFRs");
  credits += delta;
  ci.num_tx = ci.num_rx;
  ci.progress_tsc = ev_loop_tsc;

  req_pkts_pending(sslot) ? kick_req_st(sslot) : kick_rfr_st(sslot);
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

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
          if (sslot.tx_msgbuf == nullptr) continue;       // Response received
          if (sslot.client_info.req_sent == 0) continue;  // No packet sent

          assert(sslot.tx_msgbuf->get_req_num() == sslot.cur_req_num);

          size_t cycles_elapsed = rdtsc() - sslot.client_info.enqueue_req_ts;
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

    if (session->state == SessionState::kConnectInProgress ||
        session->state == SessionState::kDisconnectInProgress) {
    }
  }
}

// sslot has a valid request
template <class TTr>
void Rpc<TTr>::pkt_loss_retransmit_st(SSlot *sslot) {
  assert(in_dispatch());

  auto &ci = sslot->client_info;
  auto &credits = sslot->session->client_info.credits;

  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg,
          "eRPC Rpc %u: Packet loss suspected for session %u, req %zu. "
          "req_sent %zu, expl_cr_rcvd %zu, rfr_sent %zu, resp_rcvd %zu. Action",
          rpc_id, sslot->session->local_session_num,
          sslot->tx_msgbuf->get_req_num(), ci.req_sent, ci.expl_cr_rcvd,
          ci.rfr_sent, ci.resp_rcvd);

  if (sslot->client_info.resp_rcvd == 0) {
    // We haven't received the first response packet
    assert(ci.expl_cr_rcvd <= ci.req_sent &&
           ci.expl_cr_rcvd < sslot->tx_msgbuf->num_pkts);

    if (ci.expl_cr_rcvd == ci.req_sent) {
      LOG_DEBUG("%s: False positive. Ignoring.\n", issue_msg);
    } else {
      size_t delta = ci.req_sent - ci.expl_cr_rcvd;
      assert(credits + delta <= kSessionCredits);

      // Reclaim credits, reset progress, and add to request TX queue if needed
      LOG_DEBUG("%s: Retransmitting request.\n", issue_msg);
      credits += delta;
      ci.req_sent = ci.expl_cr_rcvd;

      sslot->client_info.enqueue_req_ts = rdtsc();
      try_req_sslot_tx_st(sslot);
    }
  } else {
    // We have received the first response packet
    assert(ci.resp_rcvd >= 1);
    if (ci.resp_rcvd - 1 == ci.rfr_sent) {
      // It's possible (but not certain) that we've received all response
      // packets, and that a background thread currently owns sslot, but it
      // cannot modify resp_rcvd or num_pkts
      LOG_DEBUG("%s: False positive. Ignoring.\n", issue_msg);
    } else {
      // We don't have the full response (which must be multi-packet), so
      // the background thread can't bury rx_msgbuf
      MsgBuffer *resp_msgbuf = sslot->client_info.resp_msgbuf;
      assert(resp_msgbuf->is_dynamic_and_matches(sslot->tx_msgbuf));
      size_t delta = ci.rfr_sent - (ci.resp_rcvd - 1);
      assert(credits + delta <= kSessionCredits);

      // Reclaim credits, reset progress, and retransmit RFR
      LOG_DEBUG("%s: Retransmitting RFR.\n", issue_msg);
      credits += delta;
      ci.rfr_sent = ci.resp_rcvd - 1;

      sslot->client_info.enqueue_req_ts = rdtsc();

      assert(credits > 0);
      credits--;  // Use one credit for this RFR
      enqueue_rfr_st(sslot, resp_msgbuf->get_pkthdr_0());
      sslot->client_info.rfr_sent++;
    }
  }
}
}  // End erpc

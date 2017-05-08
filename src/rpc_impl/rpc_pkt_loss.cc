/*
 * @file rpc_pkt_loss.cc
 * @brief Packet loss handling functions
 */
#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::pkt_loss_scan_reqs_st() {
  assert(in_creator());

  for (Session *session : session_vec) {
    // Process only connected client sessions
    if (session == nullptr || session->is_server() ||
        !session->is_connected()) {
      continue;
    }

    for (SSlot &sslot : session->sslot_arr) {
      // Ignore sslots that don't have a request with an incomplete response
      if (sslot.tx_msgbuf == nullptr) continue;
      assert(sslot.tx_msgbuf->get_req_num() == sslot.cur_req_num);

      size_t cycles_since_enqueue = rdtsc() - sslot.client_info.enqueue_req_ts;
      size_t ms_since_enqueue = to_msec(cycles_since_enqueue, nexus->freq_ghz);
      if (ms_since_enqueue >= kPktLossTimeoutMs) {
        pkt_loss_retransmit_st(&sslot);
      }
    }
  }
}

// sslot has a valid request
template <class TTr>
void Rpc<TTr>::pkt_loss_retransmit_st(SSlot *sslot) {
  assert(in_creator());
  assert(sslot != nullptr);

  auto &ci = sslot->client_info;
  auto &credits = sslot->session->client_info.credits;

  char issue_msg[kMaxIssueMsgLen];  // The basic issue message
  sprintf(issue_msg,
          "eRPC Rpc %u: Packet loss suspected for session %u, req %zu "
          "(req_sent %zu, expl_cr_rcvd %zu, resp_rcvd %zu). Action",
          rpc_id, sslot->session->local_session_num,
          sslot->tx_msgbuf->get_req_num(), ci.req_sent, ci.expl_cr_rcvd,
          ci.resp_rcvd);

  if (sslot->rx_msgbuf.is_buried()) {
    // We haven't received the first response packet
    assert(ci.expl_cr_rcvd <= ci.req_sent &&
           ci.expl_cr_rcvd < sslot->tx_msgbuf->num_pkts);

    if (ci.expl_cr_rcvd == ci.req_sent) {
      erpc_dprintf("%s: False positive. Ignoring.\n", issue_msg);
    } else {
      size_t delta = ci.req_sent - ci.expl_cr_rcvd;
      assert(credits + delta <= Session::kSessionCredits);

      // Reclaim credits, reset progress, and add to request TX queue if needed
      erpc_dprintf("%s: Retransmitting request.\n", issue_msg);
      credits += delta;  // Reclaim credits
      ci.req_sent = ci.expl_cr_rcvd;
      if (std::find(req_txq.begin(), req_txq.end(), sslot) == req_txq.end()) {
        req_txq.push_back(sslot);
      }
    }
  } else {
    // We have received the first response packet
    assert(ci.resp_rcvd >= 1);
    if (ci.resp_rcvd - 1 == ci.rfr_sent) {
      // It's possible (but not certain) that we've received all response
      // packets, and that a background thread currently owns sslot, but it
      // cannot modify resp_rcvd or num_pkts
      erpc_dprintf("%s: False positive. Ignoring.\n", issue_msg);
    } else {
      // We don't have the full response (which must be multi-packet), so
      // the background thread can't bury rx_msgbuf
      assert(sslot->rx_msgbuf.is_dynamic_and_matches(sslot->tx_msgbuf));
      size_t delta = ci.rfr_sent - (ci.resp_rcvd - 1);
      assert(credits + delta <= Session::kSessionCredits);

      // Reclaim credits, reset progress, and retransmit RFR
      erpc_dprintf("%s: Retransmitting RFR.\n", issue_msg);
      credits += delta;  // Reclaim credits
      ci.rfr_sent = ci.resp_rcvd - 1;
      send_req_for_resp_now_st(sslot, sslot->rx_msgbuf.get_pkthdr_0());
    }
  }
}
}  // End ERpc

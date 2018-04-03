#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::enqueue_rfr_st(SSlot *sslot, const pkthdr_t *resp_pkthdr) {
  assert(in_dispatch());

  MsgBuffer *ctrl_msgbuf = &ctrl_msgbufs[ctrl_msgbuf_head];
  ctrl_msgbuf_head++;
  if (ctrl_msgbuf_head == 2 * TTr::kUnsigBatch) ctrl_msgbuf_head = 0;

  // Fill in the RFR packet header. Avoid copying resp_pkthdr's headroom.
  pkthdr_t *rfr_pkthdr = ctrl_msgbuf->get_pkthdr_0();
  rfr_pkthdr->req_type = resp_pkthdr->req_type;
  rfr_pkthdr->msg_size = 0;
  rfr_pkthdr->dest_session_num = sslot->session->remote_session_num;
  rfr_pkthdr->pkt_type = kPktTypeReqForResp;
  rfr_pkthdr->pkt_num = sslot->client_info.num_tx;
  rfr_pkthdr->req_num = resp_pkthdr->req_num;
  rfr_pkthdr->magic = kPktHdrMagic;

  sslot->client_info.num_tx++;
  sslot->session->client_info.credits--;

  enqueue_hdr_tx_burst_st(
      sslot, ctrl_msgbuf,
      &sslot->client_info.tx_ts[rfr_pkthdr->pkt_num % kSessionCredits]);
}

template <class TTr>
void Rpc<TTr>::process_req_for_resp_st(SSlot *sslot, const pkthdr_t *pkthdr) {
  assert(in_dispatch());
  assert(!sslot->is_client);
  auto &si = sslot->server_info;

  // Handle reordering. If request numbers match, then we have not reset num_rx.
  assert(pkthdr->req_num <= sslot->cur_req_num);
  bool in_order =
      (pkthdr->req_num == sslot->cur_req_num) && (pkthdr->pkt_num == si.num_rx);
  if (unlikely(!in_order)) {
    char issue_msg[kMaxIssueMsgLen];
    sprintf(issue_msg,
            "eRPC Rpc %u: Received out-of-order RFR for session %u. "
            "Pkt = %zu/%zu. cur_req_num = %zu, num_rx = %zu. Action",
            rpc_id, sslot->session->local_session_num, pkthdr->req_num,
            pkthdr->pkt_num, sslot->cur_req_num, si.num_rx);

    if (pkthdr->req_num < sslot->cur_req_num || pkthdr->pkt_num > si.num_rx) {
      // Reject RFR for old requests or future packets in this request
      LOG_REORDER("%s: Dropping.\n", issue_msg);
      return;
    }

    // If we're here, this is a past RFR packet for this request. So, we still
    // have the response, and we saved request packet count.
    assert(sslot->tx_msgbuf->is_dynamic_and_matches(pkthdr));
    const size_t resp_pkt_idx = pkthdr->pkt_num - si.sav_num_req_pkts + 1;

    LOG_REORDER("%s: Re-sending response.\n", issue_msg);
    enqueue_pkt_tx_burst_st(sslot, resp_pkt_idx, nullptr);

    // Release all transport-owned buffers before re-entering event loop
    if (tx_batch_i > 0) do_tx_burst_st();
    transport->tx_flush();

    return;
  }

  sslot->server_info.num_rx++;

  const size_t resp_pkt_idx = pkthdr->pkt_num - si.sav_num_req_pkts + 1;
  enqueue_pkt_tx_burst_st(sslot, resp_pkt_idx, nullptr);
}

FORCE_COMPILE_TRANSPORTS

}  // End erpc

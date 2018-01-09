#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::enqueue_rfr_st(const SSlot *sslot, const pkthdr_t *resp_pkthdr) {
  assert(in_dispatch());
  assert(sslot->is_client);
  assert(sslot->client_info.resp_msgbuf->is_valid_dynamic() &&
         sslot->client_info.resp_msgbuf->is_resp());
  assert(resp_pkthdr->check_magic() && resp_pkthdr->is_resp());

  MsgBuffer *ctrl_msgbuf = &ctrl_msgbufs[ctrl_msgbuf_head];
  ctrl_msgbuf_head++;
  if (ctrl_msgbuf_head == 2 * TTr::kUnsigBatch) ctrl_msgbuf_head = 0;

  // Fill in the RFR packet header. Avoid copying resp_pkthdr's headroom.
  pkthdr_t *rfr_pkthdr = ctrl_msgbuf->get_pkthdr_0();
  rfr_pkthdr->req_type = resp_pkthdr->req_type;
  rfr_pkthdr->msg_size = 0;
  rfr_pkthdr->dest_session_num = sslot->session->remote_session_num;
  rfr_pkthdr->pkt_type = kPktTypeReqForResp;
  rfr_pkthdr->pkt_num = sslot->client_info.rfr_sent + 1;
  rfr_pkthdr->req_num = resp_pkthdr->req_num;
  rfr_pkthdr->magic = resp_pkthdr->magic;

  enqueue_hdr_tx_burst_st(sslot->session->remote_routing_info, ctrl_msgbuf);
}

template <class TTr>
void Rpc<TTr>::process_req_for_resp_st(SSlot *sslot, const pkthdr_t *pkthdr) {
  assert(in_dispatch());
  assert(!sslot->is_client);

  // Handle reordering
  assert(pkthdr->req_num <= sslot->cur_req_num);
  bool in_order = (pkthdr->req_num == sslot->cur_req_num) &&
                  (pkthdr->pkt_num == sslot->server_info.rfr_rcvd + 1);
  if (unlikely(!in_order)) {
    char issue_msg[kMaxIssueMsgLen];
    sprintf(issue_msg,
            "eRPC Rpc %u: Received out-of-order RFR for session %u. "
            "Pkt = %zu/%zu. cur_req_num = %zu, rfr_rcvd = %zu. Action",
            rpc_id, sslot->session->local_session_num, pkthdr->req_num,
            pkthdr->pkt_num, sslot->cur_req_num, sslot->server_info.rfr_rcvd);

    if (pkthdr->req_num < sslot->cur_req_num) {
      // Reject RFR for old requests
      LOG_DEBUG("%s: Dropping.\n", issue_msg);
      return;
    }

    if (pkthdr->pkt_num > sslot->server_info.rfr_rcvd + 1) {
      // Reject future packets
      LOG_DEBUG("%s: Dropping.\n", issue_msg);
      return;
    }

    // If we're here, this is a past RFR packet for this request. Resend resp.
    assert(pkthdr->req_num == sslot->cur_req_num &&
           pkthdr->pkt_num < sslot->server_info.rfr_rcvd + 1);
    assert(sslot->tx_msgbuf->is_dynamic_and_matches(pkthdr));

    LOG_DEBUG("%s: Re-sending response.\n", issue_msg);

    // Re-send the response packet with index = pkthdr->pkt_num (same as below)
    enqueue_pkt_tx_burst_st(sslot, pkthdr->pkt_num);

    // Release all transport-owned buffers before re-entering event loop
    if (tx_batch_i > 0) do_tx_burst_st();
    transport->tx_flush();

    return;
  }

  sslot->server_info.rfr_rcvd++;

  // Send the response packet with index = pkthdr->pktnum (same as above)
  enqueue_pkt_tx_burst_st(sslot, pkthdr->pkt_num);
}
}  // End erpc

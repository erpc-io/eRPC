#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::send_req_for_resp_now_st(const SSlot *sslot,
                                        const pkthdr_t *resp_pkthdr) {
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

}  // End erpc

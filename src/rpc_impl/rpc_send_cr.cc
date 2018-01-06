#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::send_credit_return_now_st(const Session *session,
                                         const pkthdr_t *req_pkthdr) {
  assert(in_dispatch());
  assert(session->is_server());
  assert(req_pkthdr->is_req() && req_pkthdr->check_magic());

  MsgBuffer *ctrl_msgbuf = &ctrl_msgbufs[ctrl_msgbuf_head];
  ctrl_msgbuf_head++;
  if (ctrl_msgbuf_head == 2 * TTr::kUnsigBatch) ctrl_msgbuf_head = 0;

  // Fill in the CR packet header. Avoid copying req_pkthdr's headroom.
  pkthdr_t *cr_pkthdr = ctrl_msgbuf->get_pkthdr_0();
  cr_pkthdr->req_type = req_pkthdr->req_type;
  cr_pkthdr->msg_size = 0;
  cr_pkthdr->dest_session_num = session->remote_session_num;
  cr_pkthdr->pkt_type = kPktTypeExplCR;
  cr_pkthdr->pkt_num = req_pkthdr->pkt_num;
  cr_pkthdr->req_num = req_pkthdr->req_num;
  cr_pkthdr->magic = req_pkthdr->magic;

  enqueue_hdr_tx_burst_st(session->remote_routing_info, ctrl_msgbuf);
}

}  // End erpc

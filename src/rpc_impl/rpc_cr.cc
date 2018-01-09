#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::enqueue_cr_st(const Session *session,
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

template <class TTr>
void Rpc<TTr>::process_expl_cr_st(SSlot *sslot, const pkthdr_t *pkthdr) {
  assert(in_dispatch());
  assert(sslot->is_client);

  // Handle reordering
  assert(pkthdr->req_num <= sslot->cur_req_num);
  bool in_order = (pkthdr->req_num == sslot->cur_req_num) &&
                  (pkthdr->pkt_num == sslot->client_info.expl_cr_rcvd);

  // When we roll back req_sent during packet loss recovery, for instance from 8
  // to 0, we can get credit returns for request packets 0--7 before the event
  // loop re-sends the request packets. These received packets are out of order.
  in_order &= (pkthdr->pkt_num < sslot->client_info.req_sent);

  if (unlikely(!in_order)) {
    LOG_DEBUG(
        "eRPC Rpc %u: Received out-of-order explicit CR for session %u. "
        "Pkt = %zu/%zu. cur_req_num = %zu, expl_cr_rcvd = %zu. Dropping.\n",
        rpc_id, sslot->session->local_session_num, pkthdr->req_num,
        pkthdr->pkt_num, sslot->cur_req_num, sslot->client_info.expl_cr_rcvd);
    return;
  }

  sslot->client_info.expl_cr_rcvd++;
  bump_credits(sslot->session);
}

}  // End erpc

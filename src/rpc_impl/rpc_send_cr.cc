#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::send_credit_return_now_st(const Session *session,
                                         const pkthdr_t *req_pkthdr) {
  assert(in_creator());
  assert(session->is_server());
  assert(req_pkthdr->is_req() && req_pkthdr->check_magic());

  // Fill in the CR packet header. Commented fields are copied from req_pkthdr.
  pkthdr_t cr_pkthdr = *req_pkthdr;
  // cr_pkthdr.req_type = pkthdr->req_type;
  cr_pkthdr.msg_size = 0;
  cr_pkthdr.dest_session_num = session->remote_session_num;
  cr_pkthdr.pkt_type = kPktTypeExplCR;
  // cr_pkthdr.pkt_num = pkthdr->pkt_num;
  // cr_pkthdr.req_num = pkthdr->req_num;
  // cr_pkthdr.magic = pkthdr->magic;

  // Create a "fake" static MsgBuffer for inline tx_burst
  MsgBuffer cr_msgbuf = MsgBuffer(reinterpret_cast<uint8_t *>(&cr_pkthdr), 0);
  enqueue_hdr_tx_burst_and_drain_st(session->remote_routing_info, &cr_msgbuf);
}

}  // End erpc

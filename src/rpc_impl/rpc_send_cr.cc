#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::send_credit_return_now_st(SSlot *sslot,
                                         const pkthdr_t *req_pkthdr) {
  assert(in_creator());
  assert(sslot != nullptr && sslot->session->is_server());
  assert(req_pkthdr != nullptr && req_pkthdr->check_magic());
  assert(req_pkthdr->is_req());

  // Fill in the CR packet header. Commented fields are copied from req_pkthdr.
  pkthdr_t cr_pkthdr = *req_pkthdr;
  // cr_pkthdr.req_type = pkthdr->req_type;
  cr_pkthdr.msg_size = 0;
  cr_pkthdr.dest_session_num = sslot->session->remote_session_num;
  cr_pkthdr.pkt_type = kPktTypeExplCR;
  // cr_pkthdr.pkt_num = pkthdr->pkt_num;
  // cr_pkthdr.req_num = pkthdr->req_num;
  // cr_pkthdr.magic = pkthdr->magic;

  dpath_dprintf("eRPC Rpc %u: Sending credit return packet %s (session %u).\n",
                rpc_id, cr_pkthdr.to_string().c_str(),
                sslot->session->local_session_num);

  // Create a "fake" static MsgBuffer for inline tx_burst
  MsgBuffer cr_msgbuf = MsgBuffer(reinterpret_cast<uint8_t *>(&cr_pkthdr), 0);
  tx_burst_now_st(sslot->session->remote_routing_info, &cr_msgbuf);
}

}  // End ERpc

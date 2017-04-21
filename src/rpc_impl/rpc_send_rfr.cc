#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::send_req_for_resp_now_st(SSlot *sslot, const pkthdr_t *pkthdr) {
  assert(in_creator());
  assert(sslot != nullptr && sslot->session->is_client());
  assert(pkthdr != nullptr && pkthdr->check_magic());
  assert(pkthdr->is_resp() || pkthdr->is_expl_cr());

  // Fill in the RFR packet header. Commented fields are copied from pkthdr.
  pkthdr_t rfr_pkthdr = *pkthdr;
  // rfr_pkthdr.req_type = pkthdr->req_type;
  rfr_pkthdr.msg_size = 0;
  rfr_pkthdr.dest_session_num = sslot->session->remote_session_num;
  rfr_pkthdr.pkt_type = kPktTypeReqForResp;
  rfr_pkthdr.pkt_num = sslot->clt_save_info.rfr_pkt_num++;
  rfr_pkthdr.req_num = pkthdr->req_num;
  // rfr_pkthdr.magic = pkthdr->magic;

  dpath_dprintf("eRPC Rpc %u: Sending req for resp packet %s (session %u).\n",
                rpc_id, rfr_pkthdr.to_string().c_str(),
                sslot->session->local_session_num);

  // Create a "fake" static MsgBuffer for inline tx_burst
  MsgBuffer rfr_msgbuf = MsgBuffer(reinterpret_cast<uint8_t *>(&rfr_pkthdr), 0);
  tx_burst_now_st(sslot->session->remote_routing_info, &rfr_msgbuf);
}

}  // End ERpc

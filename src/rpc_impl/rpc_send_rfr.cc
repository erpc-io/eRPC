#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::send_req_for_resp_now_st(const SSlot *sslot,
                                        const pkthdr_t *resp_pkthdr) {
  assert(in_creator());
  assert(sslot != nullptr && sslot->is_client);
  assert(sslot->client_info.resp_msgbuf->is_dynamic() &&
         sslot->client_info.resp_msgbuf->is_resp());
  assert(resp_pkthdr != nullptr && resp_pkthdr->check_magic());
  assert(resp_pkthdr->is_resp());

  // Fill in the RFR packet header. Commented fields are copied from pkthdr.
  pkthdr_t rfr_pkthdr = *resp_pkthdr;
  // rfr_pkthdr.req_type = pkthdr->req_type;
  rfr_pkthdr.msg_size = 0;
  rfr_pkthdr.dest_session_num = sslot->session->remote_session_num;
  rfr_pkthdr.pkt_type = kPktTypeReqForResp;
  rfr_pkthdr.pkt_num = sslot->client_info.rfr_sent + 1;
  // rfr_pkthdr.req_num = resp_pkthdr->req_num;
  // rfr_pkthdr.magic = pkthdr->magic;

  // Create a "fake" static MsgBuffer for inline tx_burst
  MsgBuffer rfr_msgbuf = MsgBuffer(reinterpret_cast<uint8_t *>(&rfr_pkthdr), 0);
  enqueue_hdr_tx_burst_and_drain_st(sslot->session->remote_routing_info,
                                    &rfr_msgbuf);
}

}  // End ERpc

#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::send_credit_return_now_st(Session *session,
                                         const pkthdr_t *unexp_pkthdr) {
  assert(in_creator());
  assert(session != nullptr);
  assert(unexp_pkthdr != nullptr && unexp_pkthdr->check_magic());
  assert(unexp_pkthdr->is_req() || unexp_pkthdr->is_resp());
  assert(unexp_pkthdr->is_unexp == 1);

  // Step 1: Fill in the credit return packet header. Commented fields need
  // are copied from pkthdr.
  pkthdr_t cr_pkthdr = *unexp_pkthdr;
  // cr_pkthdr.req_type = pkthdr->req_type;
  cr_pkthdr.msg_size = 0;
  cr_pkthdr.dest_session_num = session->remote_session_num;
  cr_pkthdr.pkt_type = kPktTypeCreditReturn;
  cr_pkthdr.is_unexp = 0;  // All credit returns are Expected
  cr_pkthdr.fgt_resp = 0;  // A credit return is not a response

  // cr_pkthdr.pkt_num = pkthdr->pkt_num;
  // cr_pkthdr.req_num = pkthdr->req_num;
  // cr_pkthdr.magic = pkthdr->magic;

  // Step 2: Create a "fake" static MsgBuffer for inline tx_burst
  MsgBuffer cr_msgbuf = MsgBuffer(reinterpret_cast<uint8_t *>(&cr_pkthdr), 0);
  cr_msgbuf.pkts_queued = 1;

  assert(tx_batch_i == 0);  // tx_batch_i is 0 outside rpx_tx.cc
  Transport::tx_burst_item_t &item = tx_burst_arr[0];
  item.routing_info = session->remote_routing_info;
  item.msg_buffer = &cr_msgbuf;
  item.offset = 0;
  item.data_bytes = 0;

  dpath_dprintf("eRPC Rpc %u: Sending credit return packet %s (session %u).\n",
                rpc_id, cr_pkthdr.to_string().c_str(),
                session->local_session_num);
  transport->tx_burst(tx_burst_arr, 1);
}

}  // End ERpc

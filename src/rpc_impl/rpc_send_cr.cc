#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::send_credit_return_now(Session *session) {
  assert(session != nullptr);

  // Step 1: Fill in the credit return packet header
  pkthdr_t cr_pkthdr;
  cr_pkthdr.req_type = kInvalidReqType;
  cr_pkthdr.msg_size = 0;
  cr_pkthdr.rem_session_num = session->remote_session_num;
  cr_pkthdr.pkt_type = kPktTypeCreditReturn;
  cr_pkthdr.is_unexp = 0; /* First response packet is unexpected */
  cr_pkthdr.pkt_num = 0;
  cr_pkthdr.req_num = kInvalidReqNum;
  cr_pkthdr.magic = kPktHdrMagic;

  // Step 2: Create a "fake" static MsgBuffer for inline tx_burst
  MsgBuffer cr_msgbuf = MsgBuffer((uint8_t *)&cr_pkthdr, 0);
  cr_msgbuf.pkts_queued = 1;

  assert(tx_batch_i == 0);
  tx_burst_item_t &item = tx_burst_arr[0];
  item.routing_info = session->remote_routing_info;
  item.msg_buffer = &cr_msgbuf;
  item.offset = 0;
  item.data_bytes = 0;

  dpath_dprintf("eRPC Rpc %u: Sending credit return (session %u).\n", app_tid,
                session->local_session_num);

  transport->tx_burst(tx_burst_arr, 1);
}

}  // End ERpc

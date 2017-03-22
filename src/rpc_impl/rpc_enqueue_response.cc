#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::enqueue_response(Session *session, Session::sslot_t &sslot) {
  assert(session != nullptr && session->is_server());
  assert(sslot.rx_msgbuf.buf != nullptr); /* rx_msgbuf must be valid */

  MsgBuffer *resp_msgbuf;
  app_resp_t &app_resp = sslot.app_resp;

  if (small_msg_likely(app_resp.prealloc_used)) {
    resp_msgbuf = &app_resp.pre_resp_msgbuf;
  } else {
    resp_msgbuf = &app_resp.dyn_resp_msgbuf;
  }

  /* Sanity-check resp_msgbuf */
  assert(resp_msgbuf->buf != nullptr && resp_msgbuf->check_magic());
  assert(resp_msgbuf->data_size > 0);

  // Step 1: Fill in packet 0's header
  pkthdr_t *resp_pkthdr_0 = resp_msgbuf->get_pkthdr_0();
  resp_pkthdr_0->req_type = sslot.rx_msgbuf.get_req_type();
  resp_pkthdr_0->msg_size = resp_msgbuf->data_size;
  resp_pkthdr_0->rem_session_num = session->remote_session_num;
  resp_pkthdr_0->pkt_type = kPktTypeResp;
  resp_pkthdr_0->is_unexp = 0; /* First response packet is unexpected */
  resp_pkthdr_0->pkt_num = 0;
  resp_pkthdr_0->req_num = sslot.rx_msgbuf.get_req_num();
  assert(resp_pkthdr_0->is_valid());

  // Step 2: Fill in non-zeroth packet headers, if any
  if (small_msg_unlikely(resp_msgbuf->num_pkts > 1)) {
    /*
     * Headers for non-zeroth packets are created by copying the 0th header, and
     * changing only the required fields. All non-first response packets are
     * Unexpected.
     */
    for (size_t i = 1; i < resp_msgbuf->num_pkts; i++) {
      pkthdr_t *resp_pkthdr_i = resp_msgbuf->get_pkthdr_n(i);
      *resp_pkthdr_i = *resp_pkthdr_0;
      resp_pkthdr_i->pkt_num = i;
      resp_pkthdr_i->is_unexp = 1;
    }
  }

  // Step 3: Fill in the slot, reset queueing progress, and upsert session
  // sslot.req_type filled earlier
  // sslot.req_num filled earlier
  sslot.tx_msgbuf = resp_msgbuf; /* Valid response */
  sslot.tx_msgbuf->pkts_queued = 0;

  upsert_datapath_tx_work_queue(session);
}

}  // End ERpc

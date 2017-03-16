#include "rpc.h"

namespace ERpc {

template <class Transport_>
void Rpc<Transport_>::send_response(Session *session, Session::sslot_t &sslot) {
  assert(session != nullptr && session->is_server());
  assert(sslot.is_valid());

  MsgBuffer *resp_msgbuf;
  app_resp_t &app_resp = sslot.app_resp;
  size_t resp_size = app_resp.resp_size;
  assert(resp_size > 0);

  if (small_msg_likely(app_resp.prealloc_used)) {
    /* Small response to small request */
    assert(resp_size <= Transport_::kMaxDataPerPkt);
    resp_msgbuf = &app_resp.pre_resp_msgbuf;
    resp_msgbuf->resize(resp_size, 1);
    app_resp.pre_resp_msgbuf.resize(resp_size, 1);
  } else {
    resp_msgbuf = app_resp.resp_msgbuf;
    assert(resp_msgbuf->data_size == resp_size);
  }

  /* Fill in packet 0's header */
  pkthdr_t *resp_pkthdr_0 = resp_msgbuf->get_pkthdr_0();
  resp_pkthdr_0->req_type = sslot.req_type;
  resp_pkthdr_0->msg_size = resp_msgbuf->data_size;
  resp_pkthdr_0->rem_session_num = session->client.session_num;
  resp_pkthdr_0->pkt_type = kPktTypeResp;
  resp_pkthdr_0->is_unexp = 0; /* First response packet is unexpected */
  resp_pkthdr_0->pkt_num = 0;
  resp_pkthdr_0->req_num = sslot.req_num;

  if (small_msg_likely(resp_msgbuf->num_pkts == 1)) {
    /* Small messages just need pkthdr_0, so we're done */
  } else {
    /*
     * Headers for non-zeroth packets are created by copying the 0th header, and
     * changing only the required fields. All request packets are Unexpected.
     */
    for (size_t i = 1; i < resp_msgbuf->num_pkts; i++) {
      pkthdr_t *resp_pkthdr_i = resp_msgbuf->get_pkthdr_n(i);
      *resp_pkthdr_i = *resp_pkthdr_0;
      resp_pkthdr_i->pkt_num = i;
    }
  }

  /*
   * Fill in the session slot's tx_msgbuf. This records that we have a valid
   * request for request req_num and the response.
   */
  assert(sslot.rx_msgbuf.buf != nullptr); /* Valid request */
  sslot.tx_msgbuf = resp_msgbuf;          /* Valid response */

  /* Reset queueing progress */
  sslot.tx_msgbuf->pkts_queued = 0;

  upsert_datapath_tx_work_queue(session);
}

}  // End ERpc

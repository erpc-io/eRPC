#include <stdexcept>

#include "rpc.h"

namespace ERpc {

template <class Transport_>
int Rpc<Transport_>::send_request(Session *session, uint8_t req_type,
                                  MsgBuffer *req_msgbuf) {
  if (!kDatapathChecks) {
    assert(session != nullptr && session->is_client());
    assert(session->state == SessionState::kConnected);
    assert(req_msgbuf != nullptr && req_msgbuf->is_valid());
    assert(req_msgbuf->data_size > 0 && req_msgbuf->data_size <= kMaxMsgSize);
    assert(req_msgbuf->num_pkts > 0);
  } else {
    /* If datapath checks are enabled, return meaningful error codes */
    if (unlikely(session == nullptr || !session->is_client() ||
                 session->state != SessionState::kConnected)) {
      return static_cast<int>(RpcDatapathErrCode::kInvalidSessionArg);
    }

    if (unlikely(req_msgbuf == nullptr || !req_msgbuf->is_valid())) {
      return static_cast<int>(RpcDatapathErrCode::kInvalidMsgBufferArg);
    }

    if (unlikely(req_msgbuf->data_size == 0 ||
                 req_msgbuf->data_size > kMaxMsgSize ||
                 req_msgbuf->num_pkts == 0)) {
      return static_cast<int>(RpcDatapathErrCode::kInvalidMsgBufferArg);
    }
  }

  if (unlikely(session->sslot_free_vec.size() == 0)) {
    /*
     * No free message slots left in session, so we can't queue this request.
     * This needs to be done even when kDatapathChecks is disabled.
     */
    return static_cast<int>(RpcDatapathErrCode::kNoSessionMsgSlots);
  }

  /* Find a free message slot in the session */
  size_t sslot_i = session->sslot_free_vec.pop_back();
  assert(sslot_i < Session::kSessionReqWindow);

  /* Generate the next request number for this slot */
  size_t req_num =
      (req_num_arr[sslot_i]++ * Session::kSessionReqWindow) + /* Shift */
      sslot_i;

  // Step 1: Fill in packet 0's header
  pkthdr_t *pkthdr_0 = req_msgbuf->get_pkthdr_0();
  pkthdr_0->req_type = req_type;
  pkthdr_0->msg_size = req_msgbuf->data_size;
  pkthdr_0->rem_session_num = session->server.session_num;
  pkthdr_0->pkt_type = kPktTypeReq;
  pkthdr_0->is_unexp = 1; /* Request packets are unexpected */
  pkthdr_0->pkt_num = 0;
  pkthdr_0->req_num = req_num;
  assert(pkthdr_0->is_valid());

  // Step 2: Fill in non-zeroth packet headers, if any
  if (small_msg_likely(req_msgbuf->num_pkts == 1)) {
    /* Small messages just need pkthdr_0, so we're done */
  } else {
    /*
     * Headers for non-zeroth packets are created by copying the 0th header, and
     * changing only the required fields. All request packets are Unexpected.
     */
    for (size_t i = 1; i < req_msgbuf->num_pkts; i++) {
      pkthdr_t *pkthdr_i = req_msgbuf->get_pkthdr_n(i);
      *pkthdr_i = *pkthdr_0;
      pkthdr_i->pkt_num = i;
    }
  }

  // Step 3: Fill in the slot, reset queueing progress, and upsert session
  Session::sslot_t &sslot = session->sslot_arr[sslot_i];
  assert(!sslot.is_valid());
  sslot.in_free_vec = false;
  sslot.req_type = req_type;
  sslot.req_num = req_num;
  sslot.tx_msgbuf = req_msgbuf;  /* Valid request */
  sslot.rx_msgbuf.buf = nullptr; /* Invalid response */

  sslot.tx_msgbuf->pkts_queued = 0;
  upsert_datapath_tx_work_queue(session);
  return 0;
}

}  // End ERpc

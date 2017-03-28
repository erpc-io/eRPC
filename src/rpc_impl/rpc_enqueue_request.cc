#include <stdexcept>

#include "rpc.h"

namespace ERpc {

template <class TTr>
int Rpc<TTr>::enqueue_request(int session_num, uint8_t req_type,
                              MsgBuffer *req_msgbuf, erpc_cont_func_t cont_func,
                              size_t tag) {
  assert(is_usr_session_num_in_range(session_num));
  Session *session = session_vec[(size_t)session_num];

  if (!kDatapathChecks) {
    assert(session != nullptr);
  } else {
    if (unlikely(session == nullptr)) {
      return -EINVAL;
    }
  }

  /* Prevent the creator thread from disconnecting this session */
  session_lock_cond(session);

  if (!kDatapathChecks) {
    assert(session->is_client());
    assert(session->is_connected());
    assert(req_msgbuf != nullptr);
    assert(req_msgbuf->buf != nullptr && req_msgbuf->check_magic());
    assert(req_msgbuf->data_size > 0 && req_msgbuf->data_size <= kMaxMsgSize);
    assert(req_msgbuf->num_pkts > 0);
  } else {
    /* If datapath checks are enabled, return meaningful error codes */
    if (unlikely(!session->is_client())) {
      session_unlock_cond(session);
      return -EPERM;
    }

    if (unlikely(!session->is_connected())) {
      session_unlock_cond(session);
      return -ESHUTDOWN;
    }

    if (unlikely(req_msgbuf == nullptr || req_msgbuf->buf == nullptr ||
                 !req_msgbuf->check_magic())) {
      session_unlock_cond(session);
      return -EINVAL;
    }

    if (unlikely(req_msgbuf->data_size == 0 ||
                 req_msgbuf->data_size > kMaxMsgSize ||
                 req_msgbuf->num_pkts == 0)) {
      session_unlock_cond(session);
      return -EINVAL;
    }
  }

  if (unlikely(session->sslot_free_vec.size() == 0)) {
    /*
     * No free message slots left in session, so we can't queue this request.
     * This needs to be done even when kDatapathChecks is disabled.
     */
    session_unlock_cond(session);
    return -ENOMEM;
  }

  /* Find a free message slot in the session */
  size_t sslot_i = session->sslot_free_vec.pop_back();
  assert(sslot_i < Session::kSessionReqWindow);

  /* We have an exclusive sslot, so creator thread can't disconnect now */
  session_unlock_cond(session);

  /* Generate req num. All sessions share req_num_arr, so we need to lock. */
  rpc_lock_cond();
  size_t req_num =
      (req_num_arr[sslot_i]++ * Session::kSessionReqWindow) + /* Shift */
      sslot_i;
  rpc_unlock_cond(); /* Avoid holding the lock for header-filling */

  // Fill in packet 0's header
  pkthdr_t *pkthdr_0 = req_msgbuf->get_pkthdr_0();
  pkthdr_0->req_type = req_type;
  pkthdr_0->msg_size = req_msgbuf->data_size;
  pkthdr_0->rem_session_num = session->remote_session_num;
  pkthdr_0->pkt_type = kPktTypeReq;
  pkthdr_0->is_unexp = 1; /* Request packets are unexpected */
  pkthdr_0->fgt_resp = 0; /* A request is not a response */
  pkthdr_0->pkt_num = 0;
  pkthdr_0->req_num = req_num;
  assert(pkthdr_0->check_magic());

  // Fill in non-zeroth packet headers, if any
  if (small_rpc_unlikely(req_msgbuf->num_pkts > 1)) {
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

  // Fill in the slot, reset queueing progress, and upsert session
  SSlot &sslot = session->sslot_arr[sslot_i];
  sslot.cont_func = cont_func;
  sslot.tag = tag;

  /*
   * The tx_msgbuf and rx_msgbuf (i.e., the request and response for the
   * previous request in this sslot) were buried after the response handler
   * returned.
   */
  assert(sslot.tx_msgbuf == nullptr);
  assert(sslot.rx_msgbuf.buf == nullptr &&
         sslot.rx_msgbuf.buffer.buf == nullptr);

  sslot.tx_msgbuf = req_msgbuf; /* Valid request */
  sslot.tx_msgbuf->pkts_queued = 0;

  upsert_datapath_tx_work_queue(session); /* Thread-safe */
  return 0;
}

}  // End ERpc

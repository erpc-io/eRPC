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

  // Prevent concurrent datapath/management operations on this session
  lock_cond(&session->lock);

  // Sanity checks. If datapath checks are enabled, return error codes.
  if (!kDatapathChecks) {
    assert(session->is_client());
    assert(session->is_connected());
    assert(req_msgbuf != nullptr);
    assert(req_msgbuf->buf != nullptr && req_msgbuf->check_magic() &&
           req_msgbuf->is_dynamic());
    assert(req_msgbuf->data_size > 0 && req_msgbuf->data_size <= kMaxMsgSize);
    assert(req_msgbuf->num_pkts > 0);
  } else {
    if (unlikely(!session->is_client())) {
      unlock_cond(&session->lock);
      return -EPERM;
    }

    if (unlikely(!session->is_connected())) {
      unlock_cond(&session->lock);
      return -ESHUTDOWN;
    }

    if (unlikely(req_msgbuf == nullptr || req_msgbuf->buf == nullptr ||
                 !req_msgbuf->check_magic() || !req_msgbuf->is_dynamic())) {
      unlock_cond(&session->lock);
      return -EINVAL;
    }

    if (unlikely(req_msgbuf->data_size == 0 ||
                 req_msgbuf->data_size > kMaxMsgSize ||
                 req_msgbuf->num_pkts == 0)) {
      unlock_cond(&session->lock);
      return -EINVAL;
    }
  }

  // If there are no free message slots left in session, so we can't queue this
  // request. This needs to be done even when kDatapathChecks is disabled.
  if (unlikely(session->sslot_free_vec.size() == 0)) {
    unlock_cond(&session->lock);
    return -ENOMEM;
  }

  // Grab a free message slot, and unlock the session. At this point, the
  // creator thread can't disconnect the session, and other enqueueing threads
  // can't grab this sslot.
  size_t sslot_i = session->sslot_free_vec.pop_back();
  assert(sslot_i < Session::kSessionReqWindow);
  unlock_cond(&session->lock);

  // Generate req num. All sessions share req_num_arr, so we need to lock.
  lock_cond(&req_num_arr_lock);
  size_t req_num =
      (req_num_arr[sslot_i]++ * Session::kSessionReqWindow) +  // Shift
      sslot_i;
  unlock_cond(&req_num_arr_lock);  // Avoid holding lock while header-filling

  // Fill in packet 0's header
  pkthdr_t *pkthdr_0 = req_msgbuf->get_pkthdr_0();
  pkthdr_0->req_type = req_type;
  pkthdr_0->msg_size = req_msgbuf->data_size;
  pkthdr_0->dest_session_num = session->remote_session_num;
  pkthdr_0->pkt_type = kPktTypeReq;
  pkthdr_0->is_unexp = 1;  // All request packets are unexpected
  pkthdr_0->fgt_resp = 0;  // A request is not a response
  pkthdr_0->pkt_num = 0;
  pkthdr_0->req_num = req_num;
  assert(pkthdr_0->check_magic());

  // Fill in non-zeroth packet headers, if any
  if (small_rpc_unlikely(req_msgbuf->num_pkts > 1)) {
    // Headers for non-zeroth packets are created by copying the 0th header,
    // changing only the required fields. All request packets are Unexpected.
    for (size_t i = 1; i < req_msgbuf->num_pkts; i++) {
      pkthdr_t *pkthdr_i = req_msgbuf->get_pkthdr_n(i);
      *pkthdr_i = *pkthdr_0;
      pkthdr_i->pkt_num = i;
    }
  }

  // Fill in the slot, reset queueing progress, and upsert sslot
  SSlot &sslot = session->sslot_arr[sslot_i];
  sslot.clt_save_info.cont_func = cont_func;
  sslot.clt_save_info.tag = tag;
  if (small_rpc_unlikely(multi_threaded)) {
    sslot.clt_save_info.requester_tiny_tid = get_tiny_tid();
  }

  // The tx_msgbuf and rx_msgbuf (i.e., the request and response for the
  // previous request in this sslot) were buried after the response handler
  // returned.
  assert(sslot.tx_msgbuf == nullptr);
  assert(sslot.rx_msgbuf.buf == nullptr &&
         sslot.rx_msgbuf.buffer.buf == nullptr);

  sslot.tx_msgbuf = req_msgbuf;  // Valid request
  sslot.tx_msgbuf->pkts_queued = 0;

  dpath_txq_push_back(&sslot);  // Thread-safe
  return 0;
}

}  // End ERpc

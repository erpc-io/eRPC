#include <stdexcept>

#include "rpc.h"

namespace ERpc {

template <class TTr>
int Rpc<TTr>::enqueue_request(int session_num, uint8_t req_type,
                              MsgBuffer *req_msgbuf, erpc_cont_func_t cont_func,
                              size_t tag) {
  assert(is_usr_session_num_in_range(session_num));
  Session *session = session_vec[static_cast<size_t>(session_num)];

  if (!kDatapathChecks) {
    assert(session != nullptr);
  } else {
    if (unlikely(session == nullptr)) {
      return -EINVAL;
    }
  }

  // Prevent concurrent operations on this session until we grab and fill sslot
  lock_cond(&session->lock);

  // Checks that are required even without kDatapathChecks
  if (unlikely(session->sslot_free_vec.size() == 0)) {
    unlock_cond(&session->lock);
    return -ENOMEM;
  }

  if (unlikely(!session->is_connected())) {
    unlock_cond(&session->lock);
    return -ESHUTDOWN;
  }

  // Checks that are done only if kDatapathChecks is enabled
  if (!kDatapathChecks) {
    assert(session->is_client());
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

  // Grab a free message slot, and unlock the session. At this point, the
  // creator thread can't disconnect the session.
  size_t sslot_i = session->sslot_free_vec.pop_back();
  assert(sslot_i < Session::kSessionReqWindow);

  // Fill in the sslot info
  SSlot &sslot = session->sslot_arr[sslot_i];
  // The request and response (tx_msgbuf and rx_msgbuf) for the previous request
  // in this sslot were buried when the response handler returned.
  assert(sslot.tx_msgbuf == nullptr);
  assert(sslot.rx_msgbuf.buf == nullptr &&
         sslot.rx_msgbuf.buffer.buf == nullptr);

  sslot.tx_msgbuf = req_msgbuf;      // Valid request
  sslot.tx_msgbuf->pkts_queued = 0;  // Reset queueing progress

  sslot.clt_save_info.cont_func = cont_func;
  sslot.clt_save_info.tag = tag;
  sslot.clt_save_info.enqueue_req_ts = rdtsc();

  if (optlevel_large_rpc_supported) {
    // We don't send a request-for-response for the zeroth response packet
    sslot.clt_save_info.rfr_pkt_num = 1;
  }

  if (small_rpc_unlikely(multi_threaded)) {
    if (!in_creator()) {
      sslot.clt_save_info.is_requester_bg = true;
      sslot.clt_save_info.bg_tiny_tid = get_tiny_tid();
    } else {
      sslot.clt_save_info.is_requester_bg = true;
    }
  }

  unlock_cond(&session->lock);  // Avoid holding lock while filling pkthdr

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

  // Add the sslot to request TX queue
  lock_cond(&req_txq_lock);
  assert(std::find(req_txq.begin(), req_txq.end(), &sslot) == req_txq.end());
  req_txq.push_back(&sslot);
  unlock_cond(&req_txq_lock);
  return 0;
}

}  // End ERpc

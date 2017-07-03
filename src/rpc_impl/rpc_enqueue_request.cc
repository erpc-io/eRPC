#include <stdexcept>

#include "rpc.h"

namespace ERpc {

// The cont_etid parameter is passed only when the event loop processes the
// background threads' queue of enqueue_request calls.
template <class TTr>
int Rpc<TTr>::enqueue_request(int session_num, uint8_t req_type,
                              MsgBuffer *req_msgbuf, MsgBuffer *resp_msgbuf,
                              erpc_cont_func_t cont_func, size_t tag,
                              size_t cont_etid) {
  // Since this can be called from a background thread, only do basic checks
  // that don't require accessing the session.
  assert(is_usr_session_num_in_range(session_num));
  if (!kDatapathChecks) {
    assert(req_msgbuf != nullptr);
    assert(req_msgbuf->buf != nullptr && req_msgbuf->check_magic() &&
           req_msgbuf->is_dynamic());
    assert(req_msgbuf->data_size > 0 && req_msgbuf->data_size <= kMaxMsgSize);
    assert(req_msgbuf->num_pkts > 0);

    assert(resp_msgbuf != nullptr);
    assert(resp_msgbuf->buf != nullptr && resp_msgbuf->check_magic() &&
           resp_msgbuf->is_dynamic());
    assert(resp_msgbuf->data_size > 0 && resp_msgbuf->data_size <= kMaxMsgSize);
    assert(resp_msgbuf->num_pkts > 0);
  } else {
    if (unlikely(req_msgbuf == nullptr || req_msgbuf->buf == nullptr ||
                 !req_msgbuf->check_magic() || !req_msgbuf->is_dynamic())) {
      return -EINVAL;
    }

    if (unlikely(req_msgbuf->data_size == 0 ||
                 req_msgbuf->data_size > kMaxMsgSize ||
                 req_msgbuf->num_pkts == 0)) {
      return -EINVAL;
    }

    if (unlikely(resp_msgbuf == nullptr || resp_msgbuf->buf == nullptr ||
                 !resp_msgbuf->check_magic() || !resp_msgbuf->is_dynamic())) {
      return -EINVAL;
    }

    if (unlikely(resp_msgbuf->data_size == 0 ||
                 resp_msgbuf->data_size > kMaxMsgSize ||
                 resp_msgbuf->num_pkts == 0)) {
      return -EINVAL;
    }
  }

  // When called from a background thread, enqueue to the foreground thread
  if (small_rpc_unlikely(!in_creator())) {
    assert(cont_etid == kInvalidBgETid);  // User does not specify cont TID
    auto req_args =
        enqueue_request_args_t(session_num, req_type, req_msgbuf, resp_msgbuf,
                               cont_func, tag, get_etid());
    bg_queues.enqueue_request.unlocked_push_back(req_args);
    return 0;
  }

  // If we're here, we are in the foreground thread
  assert(in_creator());

  Session *session = session_vec[static_cast<size_t>(session_num)];
  if (!kDatapathChecks) {
    assert(session != nullptr && session->is_client() &&
           session->is_connected());
  } else {
    if (unlikely(session == nullptr || !session->is_client() ||
                 !session->is_connected())) {
      return -EINVAL;
    }
  }

  // Try to grab a free session slot
  if (unlikely(session->client_info.sslot_free_vec.size() == 0)) return -EBUSY;
  size_t sslot_i = session->client_info.sslot_free_vec.pop_back();
  assert(sslot_i < Session::kSessionReqWindow);

  // Fill in the sslot info
  SSlot &sslot = session->sslot_arr[sslot_i];
  assert(sslot.tx_msgbuf == nullptr);  // Buried before calling continuation

  sslot.tx_msgbuf = req_msgbuf;
  sslot.client_info.resp_msgbuf = resp_msgbuf;

  // Fill in client-save info
  sslot.client_info.cont_func = cont_func;
  sslot.client_info.tag = tag;
  sslot.client_info.req_sent = 0;  // Reset queueing progress
  sslot.client_info.resp_rcvd = 0;

  if (optlevel_large_rpc_supported) {
    sslot.client_info.rfr_sent = 0;
    sslot.client_info.expl_cr_rcvd = 0;
  }

  sslot.client_info.enqueue_req_ts = rdtsc();

  if (small_rpc_unlikely(cont_etid != kInvalidBgETid)) {
    // We need to run the continuation in a background thread
    sslot.client_info.cont_etid = cont_etid;
  }

  sslot.cur_req_num += Session::kSessionReqWindow;  // Generate req num

  // Fill in packet 0's header
  pkthdr_t *pkthdr_0 = req_msgbuf->get_pkthdr_0();
  pkthdr_0->req_type = req_type;
  pkthdr_0->msg_size = req_msgbuf->data_size;
  pkthdr_0->dest_session_num = session->remote_session_num;
  pkthdr_0->pkt_type = kPktTypeReq;
  pkthdr_0->pkt_num = 0;
  pkthdr_0->req_num = sslot.cur_req_num;
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

  // Try to transmit small requests right now. Large requests, and small
  // requests that cannot be transmitted (e.g., due to lack of credits) are
  // queued in req_txq.
  if (small_rpc_likely(req_msgbuf->num_pkts == 1)) {
    tx_small_msg_one_st(&sslot, req_msgbuf);
    if (sslot.client_info.req_sent == 0) {
      req_txq.push_back(&sslot);
    }
  } else {
    req_txq.push_back(&sslot);
  }

  return 0;
}

}  // End ERpc

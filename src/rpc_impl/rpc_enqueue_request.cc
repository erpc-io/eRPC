#include <stdexcept>

#include "rpc.h"

namespace ERpc {

// The cont_etid parameter is passed only when the event loop processes the
// background threads' queue of enqueue_request calls.
template <class TTr>
int Rpc<TTr>::enqueue_request(int session_num, uint8_t req_type,
                              MsgBuffer *req_msgbuf, erpc_cont_func_t cont_func,
                              size_t tag, size_t cont_etid) {
  // Since this can be called from a background thread, only do basic checks
  // that don't require accessing the session.
  assert(is_usr_session_num_in_range(session_num));
  if (!kDatapathChecks) {
    assert(req_msgbuf != nullptr);
    assert(req_msgbuf->buf != nullptr && req_msgbuf->check_magic() &&
           req_msgbuf->is_dynamic());
    assert(req_msgbuf->data_size > 0 && req_msgbuf->data_size <= kMaxMsgSize);
    assert(req_msgbuf->num_pkts > 0);
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
  }

  // When called from a background thread, enqueue to the foreground thread
  if (small_rpc_unlikely(!in_creator())) {
    assert(cont_etid == kInvalidBgETid);  // User does not specify cont TID
    auto req_args = enqueue_request_args_t(session_num, req_type, req_msgbuf,
                                           cont_func, tag, get_etid());
    bg_queues.enqueue_request.unlocked_push_back(req_args);
    return 0;
  }

  // If we're here, we are in the foreground thread
  assert(in_creator());

  Session *session = session_vec[static_cast<size_t>(session_num)];
  if (!kDatapathChecks) {
    assert(session != nullptr);
    assert(session->is_client());
  } else {
    if (unlikely(session == nullptr)) return -EINVAL;
    if (unlikely(!session->is_client())) return -EPERM;
  }

  // Try to grab a free session slot
  if (unlikely(session->client_info.sslot_free_vec.size() == 0)) return -ENOMEM;
  size_t sslot_i = session->client_info.sslot_free_vec.pop_back();
  assert(sslot_i < Session::kSessionReqWindow);

  // Fill in the sslot info
  SSlot &sslot = session->sslot_arr[sslot_i];
  // The request and response (tx_msgbuf and rx_msgbuf) for the previous request
  // in this sslot were buried when the response handler returned.
  assert(sslot.tx_msgbuf == nullptr);
  assert(sslot.rx_msgbuf.is_buried());

  sslot.tx_msgbuf = req_msgbuf;
  sslot.pkts_queued = 0;
  sslot.pkts_rcvd = 0;

  // Fill in client-save info
  sslot.client_info.cont_func = cont_func;
  sslot.client_info.tag = tag;
  sslot.client_info.enqueue_req_ts = rdtsc();

  if (optlevel_large_rpc_supported) {
    // We don't send a request-for-response for the zeroth response packet
    sslot.client_info.rfr_pkt_num = 1;
  }

  if (small_rpc_unlikely(cont_etid != kInvalidBgETid)) {
    // We need to run the continuation in a background thread
    sslot.client_info.cont_etid = cont_etid;
  }

  // Generate req num
  size_t req_num = (sslot.cur_req_num++ * Session::kSessionReqWindow) + sslot_i;

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

  req_txq.push_back(&sslot);
  return 0;
}

}  // End ERpc

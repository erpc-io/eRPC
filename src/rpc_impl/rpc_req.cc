#include <stdexcept>

#include "rpc.h"

namespace erpc {

// The cont_etid parameter is passed only when the event loop processes the
// background threads' queue of enqueue_request calls.
template <class TTr>
int Rpc<TTr>::enqueue_request(int session_num, uint8_t req_type,
                              MsgBuffer *req_msgbuf, MsgBuffer *resp_msgbuf,
                              erpc_cont_func_t cont_func, size_t tag,
                              size_t cont_etid) {
  // Since this can be called from a background thread, only do basic checks
  // that don't require accessing the session.
  assert(req_msgbuf->is_valid_dynamic());
  assert(req_msgbuf->data_size > 0 && req_msgbuf->data_size <= kMaxMsgSize);
  assert(req_msgbuf->num_pkts > 0);

  assert(resp_msgbuf->is_valid_dynamic());

  // The current size of resp_msgbuf can be 0
  assert(resp_msgbuf->max_data_size > 0 &&
         resp_msgbuf->max_data_size <= kMaxMsgSize);
  assert(resp_msgbuf->max_num_pkts > 0);

  // When called from a background thread, enqueue to the foreground thread
  if (unlikely(!in_dispatch())) {
    assert(cont_etid == kInvalidBgETid);  // User does not specify cont TID
    auto req_args =
        enqueue_request_args_t(session_num, req_type, req_msgbuf, resp_msgbuf,
                               cont_func, tag, get_etid());
    bg_queues.enqueue_request.unlocked_push(req_args);
    return 0;
  }

  // If we're here, we're in the dispatch thread
  assert(is_usr_session_num_in_range_st(session_num));
  Session *session = session_vec[static_cast<size_t>(session_num)];

  // We never disconnect a session before notifying the eRPC user, so we don't
  // need to catch this behavior
  assert(session != nullptr && session->is_client() && session->is_connected());

  // Try to grab a free session slot
  if (unlikely(session->client_info.sslot_free_vec.size() == 0)) return -EBUSY;
  size_t sslot_i = session->client_info.sslot_free_vec.pop_back();
  assert(sslot_i < kSessionReqWindow);

  // Fill in the sslot info
  SSlot &sslot = session->sslot_arr[sslot_i];
  assert(sslot.tx_msgbuf == nullptr);  // Previous response was received
  sslot.tx_msgbuf = req_msgbuf;        // Mark the request as active/incomplete

  sslot.client_info.resp_msgbuf = resp_msgbuf;

  // Fill in client-save info
  sslot.client_info.cont_func = cont_func;
  sslot.client_info.tag = tag;
  sslot.client_info.req_sent = 0;  // Reset queueing progress
  sslot.client_info.resp_rcvd = 0;
  sslot.client_info.rfr_sent = 0;
  sslot.client_info.expl_cr_rcvd = 0;

  sslot.client_info.enqueue_req_ts = rdtsc();

  if (unlikely(cont_etid != kInvalidBgETid)) {
    // We need to run the continuation in a background thread
    sslot.client_info.cont_etid = cont_etid;
  }

  sslot.cur_req_num += kSessionReqWindow;  // Generate req num

  // Fill in packet 0's header
  pkthdr_t *pkthdr_0 = req_msgbuf->get_pkthdr_0();
  pkthdr_0->req_type = req_type;
  pkthdr_0->msg_size = req_msgbuf->data_size;
  pkthdr_0->dest_session_num = session->remote_session_num;
  pkthdr_0->pkt_type = kPktTypeReq;
  pkthdr_0->pkt_num = 0;
  pkthdr_0->req_num = sslot.cur_req_num;

  // Fill in non-zeroth packet headers, if any
  if (unlikely(req_msgbuf->num_pkts > 1)) {
    // Headers for non-zeroth packets are created by copying the 0th header,
    // changing only the required fields. All request packets are Unexpected.
    for (size_t i = 1; i < req_msgbuf->num_pkts; i++) {
      pkthdr_t *pkthdr_i = req_msgbuf->get_pkthdr_n(i);
      *pkthdr_i = *pkthdr_0;
      pkthdr_i->pkt_num = i;
    }
  }

  try_req_sslot_tx_st(&sslot);
  if (sslot.client_info.req_sent != req_msgbuf->num_pkts) {
    crd_stall_txq.push_back(&sslot);
  }

  return 0;
}

template <class TTr>
void Rpc<TTr>::try_req_sslot_tx_st(SSlot *sslot) {
  MsgBuffer *req_msgbuf = sslot->tx_msgbuf;
  Session *session = sslot->session;
  assert(session->is_client() && session->is_connected());
  assert(req_msgbuf->is_valid_dynamic() && req_msgbuf->is_req());
  assert(sslot->client_info.req_sent < req_msgbuf->num_pkts);

  if (session->client_info.credits > 0) {
    if (likely(req_msgbuf->num_pkts == 1)) {
      // Small request
      session->client_info.credits--;
      enqueue_pkt_tx_burst_st(sslot, 0);
      sslot->client_info.req_sent++;
    } else {
      // Large request
      size_t now_sending =
          std::min(session->client_info.credits,
                   req_msgbuf->num_pkts - sslot->client_info.req_sent);
      assert(now_sending > 0);
      session->client_info.credits -= now_sending;

      for (size_t _x = 0; _x < now_sending; _x++) {
        enqueue_pkt_tx_burst_st(sslot, sslot->client_info.req_sent);
        sslot->client_info.req_sent++;
      }
    }
  } else {
    dpath_stat_inc(session->dpath_stats.credits_exhaused, 1);
  }
}

template <class TTr>
void Rpc<TTr>::process_small_req_st(SSlot *sslot, pkthdr_t *pkthdr) {
  assert(in_dispatch());
  assert(!sslot->is_client);

  // Handle reordering
  if (unlikely(pkthdr->req_num <= sslot->cur_req_num)) {
    char issue_msg[kMaxIssueMsgLen];
    sprintf(issue_msg,
            "eRPC Rpc %u: Received out-of-order request for session %u. "
            "Req num: %zu (pkt), %zu (sslot). Action",
            rpc_id, sslot->session->local_session_num, pkthdr->req_num,
            sslot->cur_req_num);

    if (pkthdr->req_num < sslot->cur_req_num) {
      // This is a massively-delayed retransmission of an old request
      LOG_DEBUG("%s: Dropping.\n", issue_msg);
      return;
    } else {
      // This is a retransmission for the currently active request
      assert(sslot->server_info.req_rcvd == 1);

      if (sslot->tx_msgbuf != nullptr) {
        // The response is available, so resend it
        assert(sslot->tx_msgbuf->get_req_num() == sslot->cur_req_num);
        assert(sslot->tx_msgbuf->is_resp());
        assert(sslot->tx_msgbuf->is_dynamic_and_matches(pkthdr));

        LOG_DEBUG("%s: Re-sending response.\n", issue_msg);
        enqueue_pkt_tx_burst_st(sslot, 0);

        // Release all transport-owned buffers before re-entering event loop
        if (tx_batch_i > 0) do_tx_burst_st();
        transport->tx_flush();
        return;
      } else {
        LOG_DEBUG("%s: Response not available yet. Dropping.\n", issue_msg);
        return;
      }
    }
  }

  // If we're here, this is the first (and only) packet of this new request
  assert(pkthdr->req_num == sslot->cur_req_num + kSessionReqWindow);

  auto &req_msgbuf = sslot->server_info.req_msgbuf;
  assert(req_msgbuf.is_buried());  // Buried on prev req's enqueue_response()

  // Update sslot tracking
  sslot->cur_req_num = pkthdr->req_num;
  sslot->server_info.req_rcvd = 1;

  // Bury the previous, possibly dynamic response (sslot->tx_msgbuf). This marks
  // the response for cur_req_num as unavailable.
  bury_resp_msgbuf_server_st(sslot);

  const ReqFunc &req_func = req_func_arr[pkthdr->req_type];
  assert(req_func.is_registered());

  // Remember request metadata for enqueue_response(). req_type was invalidated
  // on previous enqueue_response(). Setting it implies that an enqueue_resp()
  // is now pending; this invariant is used to safely reset sessions.
  assert(sslot->server_info.req_type == kInvalidReqType);
  sslot->server_info.req_type = pkthdr->req_type;
  sslot->server_info.req_func_type = req_func.req_func_type;

  if (likely(!req_func.is_background())) {
    // For foreground request handlers, a "fake" static request MsgBuffer
    // suffices -- it's valid for the duration of req_func().
    req_msgbuf = MsgBuffer(pkthdr, pkthdr->msg_size);
    req_func.req_func(static_cast<ReqHandle *>(sslot), context);
    return;
  } else {
    // For background request handlers, we need a RX ring--independent copy of
    // the request. The allocated req_msgbuf is freed by the background thread.
    req_msgbuf = alloc_msg_buffer(pkthdr->msg_size);
    assert(req_msgbuf.buf != nullptr);
    memcpy(req_msgbuf.get_pkthdr_0(), pkthdr,
           pkthdr->msg_size + sizeof(pkthdr_t));
    submit_background_st(sslot, Nexus::BgWorkItemType::kReq);
    return;
  }
}

template <class TTr>
void Rpc<TTr>::process_large_req_one_st(SSlot *sslot, const pkthdr_t *pkthdr) {
  assert(in_dispatch());
  assert(!sslot->is_client);
  MsgBuffer &req_msgbuf = sslot->server_info.req_msgbuf;

  // Handle reordering
  bool is_next_pkt_same_req =  // Is this the next packet in this request?
      (pkthdr->req_num == sslot->cur_req_num) &&
      (pkthdr->pkt_num == sslot->server_info.req_rcvd);
  bool is_first_pkt_next_req =  // Is this the first packet in the next request?
      (pkthdr->req_num == sslot->cur_req_num + kSessionReqWindow) &&
      (pkthdr->pkt_num == 0);

  bool in_order = is_next_pkt_same_req || is_first_pkt_next_req;
  if (unlikely(!in_order)) {
    char issue_msg[kMaxIssueMsgLen];
    sprintf(issue_msg,
            "eRPC Rpc %u: Received out-of-order request for session %u. "
            "Req/pkt numbers: %zu/%zu (pkt), %zu/%zu (sslot). Action",
            rpc_id, sslot->session->local_session_num, pkthdr->req_num,
            pkthdr->pkt_num, sslot->cur_req_num, sslot->server_info.req_rcvd);

    // Only past packets belonging to this request are not dropped
    if (pkthdr->req_num != sslot->cur_req_num ||
        pkthdr->pkt_num > sslot->server_info.req_rcvd) {
      LOG_DEBUG("%s: Dropping.\n", issue_msg);
      return;
    }

    // req_msgbuf could be buried if we've received the entire request and
    // queued the response, so directly compute the number of packets in request
    size_t num_pkts_in_req = data_size_to_num_pkts(pkthdr->msg_size);
    if (sslot->server_info.req_rcvd != num_pkts_in_req) {
      assert(req_msgbuf.is_dynamic_and_matches(pkthdr));
    }

    if (pkthdr->pkt_num != num_pkts_in_req - 1) {
      // This is not the last packet in the request => send a credit return
      LOG_DEBUG("%s: Re-sending credit return.\n", issue_msg);

      enqueue_cr_st(sslot, pkthdr);  // tx_flush uneeded. XXX: why?
      return;
    }

    // This is the last request packet, so re-send response if it's available
    if (sslot->tx_msgbuf != nullptr) {
      // The response is available, so resend it
      assert(sslot->tx_msgbuf->get_req_num() == sslot->cur_req_num);
      assert(sslot->tx_msgbuf->is_resp());
      assert(sslot->tx_msgbuf->is_dynamic_and_matches(pkthdr));

      LOG_DEBUG("%s: Re-sending response.\n", issue_msg);
      enqueue_pkt_tx_burst_st(sslot, 0);

      // Release all transport-owned buffers before re-entering event loop
      if (tx_batch_i > 0) do_tx_burst_st();
      transport->tx_flush();
    } else {
      // The response is not available yet, client will have to timeout again
      LOG_DEBUG("%s: Dropping because response not available yet.\n",
                issue_msg);
    }
    return;
  }

  // Allocate or locate the request MsgBuffer
  if (pkthdr->pkt_num == 0) {
    // This is the first packet received for this request
    assert(req_msgbuf.is_buried());  // Buried on prev req's enqueue_response()

    // Update sslot tracking
    sslot->cur_req_num = pkthdr->req_num;
    sslot->server_info.req_rcvd = 1;

    // Bury the previous, possibly dynamic response. This marks the response for
    // cur_req_num as unavailable.
    bury_resp_msgbuf_server_st(sslot);

    req_msgbuf = alloc_msg_buffer(pkthdr->msg_size);
    assert(req_msgbuf.buf != nullptr);
    *(req_msgbuf.get_pkthdr_0()) = *pkthdr;  // Copy packet header
  } else {
    // This is not the first packet for this request
    assert(req_msgbuf.is_dynamic_and_matches(pkthdr));
    assert(sslot->server_info.req_rcvd >= 1);
    assert(sslot->cur_req_num == pkthdr->req_num);

    sslot->server_info.req_rcvd++;
  }

  // Send a credit return for every request packet except the last in sequence
  if (pkthdr->pkt_num != req_msgbuf.num_pkts - 1) enqueue_cr_st(sslot, pkthdr);

  copy_data_to_msgbuf(&req_msgbuf, pkthdr);  // Header 0 was copied earlier

  // Invoke the request handler iff we have all the request packets
  if (sslot->server_info.req_rcvd != req_msgbuf.num_pkts) return;

  const ReqFunc &req_func = req_func_arr[pkthdr->req_type];
  assert(req_func.is_registered());

  // Remember request metadata for enqueue_response(). req_type was invalidated
  // on previous enqueue_response(). Setting it implies that an enqueue_resp()
  // is now pending; this invariant is used to safely reset sessions.
  assert(sslot->server_info.req_type == kInvalidReqType);
  sslot->server_info.req_type = pkthdr->req_type;
  sslot->server_info.req_func_type = req_func.req_func_type;

  // req_msgbuf here is independent of the RX ring, so don't make another copy
  if (!req_func.is_background()) {
    req_func.req_func(static_cast<ReqHandle *>(sslot), context);
  } else {
    submit_background_st(sslot, Nexus::BgWorkItemType::kReq);
  }
}
}  // End erpc

#include <stdexcept>

#include "rpc.h"

namespace erpc {

// The cont_etid parameter is passed only when the event loop processes the
// background threads' queue of enqueue_request calls.
template <class TTr>
void Rpc<TTr>::enqueue_request(int session_num, uint8_t req_type,
                               MsgBuffer *req_msgbuf, MsgBuffer *resp_msgbuf,
                               erpc_cont_func_t cont_func, void *tag,
                               size_t cont_etid) {
  // When called from a background thread, enqueue to the foreground thread
  if (unlikely(!in_dispatch())) {
    auto req_args = enq_req_args_t(session_num, req_type, req_msgbuf,
                                   resp_msgbuf, cont_func, tag, get_etid());
    bg_queues._enqueue_request.unlocked_push(req_args);
    return;
  }

  // If we're here, we're in the dispatch thread
  Session *session = session_vec[static_cast<size_t>(session_num)];
  assert(session->is_connected());  // User is notified before we disconnect

  // If a free sslot is unavailable, save to session backlog
  if (unlikely(session->client_info.sslot_free_vec.size() == 0)) {
    session->client_info.enq_req_backlog.emplace(session_num, req_type,
                                                 req_msgbuf, resp_msgbuf,
                                                 cont_func, tag, cont_etid);
    return;
  }

  // Fill in the sslot info
  size_t sslot_i = session->client_info.sslot_free_vec.pop_back();
  SSlot &sslot = session->sslot_arr[sslot_i];
  assert(sslot.tx_msgbuf == nullptr);  // Previous response was received
  sslot.tx_msgbuf = req_msgbuf;        // Mark the request as active/incomplete
  sslot.cur_req_num += kSessionReqWindow;  // Move to next request

  auto &ci = sslot.client_info;
  ci.resp_msgbuf = resp_msgbuf;
  ci.cont_func = cont_func;
  ci.tag = tag;
  ci.progress_tsc = ev_loop_tsc;
  add_to_active_rpc_list(sslot);

  ci.num_rx = 0;
  ci.num_tx = 0;
  ci.cont_etid = cont_etid;

  // Fill in packet 0's header
  pkthdr_t *pkthdr_0 = req_msgbuf->get_pkthdr_0();
  pkthdr_0->req_type = req_type;
  pkthdr_0->msg_size = req_msgbuf->data_size;
  pkthdr_0->dest_session_num = session->remote_session_num;
  pkthdr_0->pkt_type = kPktTypeReq;
  pkthdr_0->pkt_num = 0;
  pkthdr_0->req_num = sslot.cur_req_num;

  // Fill in any non-zeroth packet headers, using pkthdr_0 as the base.
  if (unlikely(req_msgbuf->num_pkts > 1)) {
    for (size_t i = 1; i < req_msgbuf->num_pkts; i++) {
      pkthdr_t *pkthdr_i = req_msgbuf->get_pkthdr_n(i);
      memcpy(pkthdr_i, pkthdr_0, sizeof(pkthdr_t));
      pkthdr_i->pkt_num = i;
    }
  }

  if (likely(session->client_info.credits > 0)) {
    kick_req_st(&sslot);
  } else {
    stallq.push_back(&sslot);
  }
}

template <class TTr>
void Rpc<TTr>::process_small_req_st(SSlot *sslot, pkthdr_t *pkthdr) {
  assert(in_dispatch());

  // Handle reordering
  if (unlikely(pkthdr->req_num <= sslot->cur_req_num)) {
    char issue_msg[kMaxIssueMsgLen];
    sprintf(issue_msg,
            "Rpc %u, lsn %u (%s): Received out-of-order request for session. "
            "Req num: %zu (pkt), %zu (sslot). Action",
            rpc_id, sslot->session->local_session_num,
            sslot->session->get_remote_hostname().c_str(), pkthdr->req_num,
            sslot->cur_req_num);

    if (pkthdr->req_num < sslot->cur_req_num) {
      // This is a massively-delayed retransmission of an old request
      ERPC_REORDER("%s: Dropping.\n", issue_msg);
      return;
    } else {
      // This is a retransmission for the currently active request
      if (sslot->tx_msgbuf != nullptr) {
        // The response is available, so resend this req's corresponding packet
        ERPC_REORDER("%s: Re-sending response.\n", issue_msg);
        enqueue_pkt_tx_burst_st(sslot, 0, nullptr);  // Packet index = 0
        drain_tx_batch_and_dma_queue();
        return;
      } else {
        ERPC_REORDER("%s: Response not available yet. Dropping.\n", issue_msg);
        return;
      }
    }
  }

  // If we're here, this is the first (and only) packet of this new request
  assert(pkthdr->req_num == sslot->cur_req_num + kSessionReqWindow);

  auto &req_msgbuf = sslot->server_info.req_msgbuf;
  assert(req_msgbuf.is_buried());  // Buried on prev req's enqueue_response()

  // Bury the previous, possibly dynamic response (sslot->tx_msgbuf). This marks
  // the response for cur_req_num as unavailable.
  bury_resp_msgbuf_server_st(sslot);

  // Update sslot tracking
  sslot->cur_req_num = pkthdr->req_num;
  sslot->server_info.num_rx = 1;

  const ReqFunc &req_func = req_func_arr[pkthdr->req_type];

  // Remember request metadata for enqueue_response(). req_type was invalidated
  // on previous enqueue_response(). Setting it implies that an enqueue_resp()
  // is now pending; this invariant is used to safely reset sessions.
  assert(sslot->server_info.req_type == kInvalidReqType);
  sslot->server_info.req_type = pkthdr->req_type;
  sslot->server_info.req_func_type = req_func.req_func_type;

  if (likely(!req_func.is_background())) {
    if (kZeroCopyRX) {
      // For foreground request handlers, a "fake" static request msgbuf
      // suffices. This improves performance, but it restricts ownership of the
      // request msgbuf to the duration of req_func.
      req_msgbuf = MsgBuffer(pkthdr, pkthdr->msg_size);
    } else {
      req_msgbuf = alloc_msg_buffer(pkthdr->msg_size);
      memcpy(req_msgbuf.buf, pkthdr + 1, pkthdr->msg_size);  // Omit header
    }
    req_func.req_func(static_cast<ReqHandle *>(sslot), context);
    return;
  } else {
    // Background request handlers need an RX ring--independent request copy
    req_msgbuf = alloc_msg_buffer(pkthdr->msg_size);
    memcpy(req_msgbuf.buf, pkthdr + 1, pkthdr->msg_size);  // Omit header
    submit_bg_req_st(sslot);
    return;
  }
}

template <class TTr>
void Rpc<TTr>::process_large_req_one_st(SSlot *sslot, const pkthdr_t *pkthdr) {
  assert(in_dispatch());

  // Handle reordering
  bool is_next_pkt_same_req =  // Is this the next packet in this request?
      (pkthdr->req_num == sslot->cur_req_num) &&
      (pkthdr->pkt_num == sslot->server_info.num_rx);
  bool is_first_pkt_next_req =  // Is this the first packet in the next request?
      (pkthdr->req_num == sslot->cur_req_num + kSessionReqWindow) &&
      (pkthdr->pkt_num == 0);

  bool in_order = is_next_pkt_same_req || is_first_pkt_next_req;
  if (unlikely(!in_order)) {
    char issue_msg[kMaxIssueMsgLen];
    sprintf(issue_msg,
            "Rpc %u, lsn %u: Received out-of-order request. "
            "Req/pkt numbers: %zu/%zu (pkt), %zu/%zu (sslot). Action",
            rpc_id, sslot->session->local_session_num, pkthdr->req_num,
            pkthdr->pkt_num, sslot->cur_req_num, sslot->server_info.num_rx);

    // Only past packets belonging to this request are not dropped
    if (pkthdr->req_num != sslot->cur_req_num ||
        pkthdr->pkt_num > sslot->server_info.num_rx) {
      ERPC_REORDER("%s: Dropping.\n", issue_msg);
      return;
    }

    // If this is not the last packet in the request, send a credit return.
    //
    // req_msgbuf could be buried if we have received the entire request and
    // queued the response, so directly compute number of packets in request.
    if (pkthdr->pkt_num != data_size_to_num_pkts(pkthdr->msg_size) - 1) {
      ERPC_REORDER("%s: Re-sending credit return.\n", issue_msg);
      enqueue_cr_st(sslot, pkthdr);  // Header only, so tx_flush uneeded
      return;
    }

    // This is the last request packet, so re-send response if it's available
    if (sslot->tx_msgbuf != nullptr) {
      // The response is available, so resend it
      ERPC_REORDER("%s: Re-sending response.\n", issue_msg);
      enqueue_pkt_tx_burst_st(sslot, 0, nullptr);  // Packet index = 0
      drain_tx_batch_and_dma_queue();
    } else {
      // The response is not available yet, client will have to timeout again
      ERPC_REORDER("%s: Dropping because response not available yet.\n",
                   issue_msg);
    }
    return;
  }

  MsgBuffer &req_msgbuf = sslot->server_info.req_msgbuf;

  // Allocate or locate the request MsgBuffer
  if (pkthdr->pkt_num == 0) {
    // This is the first packet received for this request
    assert(req_msgbuf.is_buried());  // Buried on prev req's enqueue_response()

    // Bury the previous, possibly dynamic response. This marks the response for
    // cur_req_num as unavailable.
    bury_resp_msgbuf_server_st(sslot);

    req_msgbuf = alloc_msg_buffer(pkthdr->msg_size);
    assert(req_msgbuf.buf != nullptr);

    // Update sslot tracking
    sslot->cur_req_num = pkthdr->req_num;
    sslot->server_info.num_rx = 1;
  } else {
    // This is not the first packet for this request
    sslot->server_info.num_rx++;
  }

  // Send a credit return for every request packet except the last in sequence
  if (pkthdr->pkt_num != req_msgbuf.num_pkts - 1) enqueue_cr_st(sslot, pkthdr);

  copy_data_to_msgbuf(&req_msgbuf, pkthdr->pkt_num, pkthdr);  // Omits header

  // Invoke the request handler iff we have all the request packets
  if (sslot->server_info.num_rx != req_msgbuf.num_pkts) return;

  const ReqFunc &req_func = req_func_arr[pkthdr->req_type];

  // Remember request metadata for enqueue_response(). req_type was invalidated
  // on previous enqueue_response(). Setting it implies that an enqueue_resp()
  // is now pending; this invariant is used to safely reset sessions.
  assert(sslot->server_info.req_type == kInvalidReqType);
  sslot->server_info.req_type = pkthdr->req_type;
  sslot->server_info.req_func_type = req_func.req_func_type;

  // req_msgbuf here is independent of the RX ring, so don't make another copy
  if (likely(!req_func.is_background())) {
    req_func.req_func(static_cast<ReqHandle *>(sslot), context);
  } else {
    submit_bg_req_st(sslot);
  }
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

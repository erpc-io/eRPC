#include "rpc.h"

namespace erpc {

// For both foreground and background request handlers, enqueue_response() may
// be called before or after the request handler returns to the event loop, at
// which point the event loop buries the request MsgBuffer.
//
// So sslot->rx_msgbuf may or may not be valid at this point.
template <class TTr>
void Rpc<TTr>::enqueue_response(ReqHandle *req_handle) {
  // When called from a background thread, enqueue to the foreground thread
  if (unlikely(!in_dispatch())) {
    bg_queues.enqueue_response.unlocked_push(req_handle);
    return;
  }

  // If we're here, we're in the dispatch thread
  SSlot *sslot = static_cast<SSlot *>(req_handle);
  bury_req_msgbuf_server_st(sslot);  // Bury the possibly-dynamic req MsgBuffer

  Session *session = sslot->session;
  assert(session != nullptr && session->is_server());

  if (unlikely(!session->is_connected())) {
    // A session reset could be waiting for this enqueue_response()
    assert(session->state == SessionState::kResetInProgress);

    LOG_WARN(
        "eRPC Rpc %u: enqueue_response() for reset-in-progress session %u.\n",
        rpc_id, session->local_session_num);

    // Mark enqueue_response() as completed
    assert(sslot->server_info.req_type != kInvalidReqType);
    sslot->server_info.req_type = kInvalidReqType;

    return;  // During session reset, don't add packets to TX burst
  }

  MsgBuffer *resp_msgbuf =
      sslot->prealloc_used ? &sslot->pre_resp_msgbuf : &sslot->dyn_resp_msgbuf;
  assert(resp_msgbuf->is_valid_dynamic() && resp_msgbuf->data_size > 0);

  // Fill in packet 0's header
  pkthdr_t *resp_pkthdr_0 = resp_msgbuf->get_pkthdr_0();
  resp_pkthdr_0->req_type = sslot->server_info.req_type;
  resp_pkthdr_0->msg_size = resp_msgbuf->data_size;
  resp_pkthdr_0->dest_session_num = session->remote_session_num;
  resp_pkthdr_0->pkt_type = kPktTypeResp;
  resp_pkthdr_0->pkt_num = 0;
  resp_pkthdr_0->req_num = sslot->cur_req_num;

  // Fill in non-zeroth packet headers, if any
  if (resp_msgbuf->num_pkts > 1) {
    // Headers for non-zeroth packets are created by copying the 0th header, and
    // changing only the required fields.
    for (size_t i = 1; i < resp_msgbuf->num_pkts; i++) {
      pkthdr_t *resp_pkthdr_i = resp_msgbuf->get_pkthdr_n(i);
      *resp_pkthdr_i = *resp_pkthdr_0;
      resp_pkthdr_i->pkt_num = i;
    }
  }

  // Fill in the slot and reset queueing progress
  assert(sslot->tx_msgbuf == nullptr);  // Buried before calling request handler
  sslot->tx_msgbuf = resp_msgbuf;       // Mark response as valid
  sslot->server_info.rfr_rcvd = 0;

  // Mark enqueue_response() as completed
  assert(sslot->server_info.req_type != kInvalidReqType);
  sslot->server_info.req_type = kInvalidReqType;

  enqueue_pkt_tx_burst_st(sslot, 0, nullptr);  // Enqueue zeroth response packet
}

template <class TTr>
void Rpc<TTr>::process_small_resp_st(SSlot *sslot, const pkthdr_t *pkthdr) {
  assert(in_dispatch());
  assert(sslot->is_client);
  assert(pkthdr->req_num <= sslot->cur_req_num);  // Response from the future?

  // Handle reordering
  bool in_order = (pkthdr->req_num == sslot->cur_req_num) &&
                  (sslot->client_info.resp_rcvd == 0);

  if (likely(in_order)) {
    // resp_rcvd == 0 means that we haven't received the response before now,
    // so the request MsgBuffer (tx_msgbuf) is valid.
    assert(sslot->tx_msgbuf != nullptr &&
           sslot->tx_msgbuf->is_dynamic_and_matches(pkthdr));

    // When we roll back req_sent during packet loss recovery, for instance
    // from 8 to 7 for an 8-packet request, we can get response packet 0 before
    // the event loop re-sends the 8th request packet. This received response
    // packet is out-of-order.
    in_order &= (sslot->client_info.req_sent == sslot->tx_msgbuf->num_pkts);
  }

  if (unlikely(!in_order)) {
    LOG_DEBUG(
        "eRPC Rpc %u: Received out-of-order response for session %u. "
        "Request num: %zu (pkt), %zu (sslot). Dropping.\n",
        rpc_id, sslot->session->local_session_num, pkthdr->req_num,
        sslot->cur_req_num);
    return;
  }

  if (kCC) {
    size_t rtt_tsc = dpath_rdtsc() - sslot->client_info.tx_ts[0];
    sslot->session->client_info.cc.timely.update_rate(rtt_tsc);
  }

  // If we're here, this is the first (and only) packet of the response
  assert(sslot->tx_msgbuf->is_dynamic_and_matches(pkthdr));  // Check request

  MsgBuffer *resp_msgbuf = sslot->client_info.resp_msgbuf;
  assert(resp_msgbuf->max_data_size >= pkthdr->msg_size);
  resize_msg_buffer(resp_msgbuf, pkthdr->msg_size);

  sslot->client_info.resp_rcvd = 1;
  bump_credits(sslot->session);
  sslot->tx_msgbuf = nullptr;  // Mark response as received

  // Copy header and data, but not headroom
  memcpy(reinterpret_cast<uint8_t *>(resp_msgbuf->get_pkthdr_0()) + kHeadroom,
         reinterpret_cast<const uint8_t *>(pkthdr) + kHeadroom,
         pkthdr->msg_size + sizeof(pkthdr_t) - kHeadroom);

  if (likely(sslot->client_info.cont_etid == kInvalidBgETid)) {
    sslot->client_info.cont_func(static_cast<RespHandle *>(sslot), context,
                                 sslot->client_info.tag);
  } else {
    // Background thread will run continuation
    submit_background_st(sslot, Nexus::BgWorkItemType::kResp,
                         sslot->client_info.cont_etid);
    return;
  }
}

template <class TTr>
void Rpc<TTr>::process_large_resp_one_st(SSlot *sslot, const pkthdr_t *pkthdr) {
  assert(in_dispatch());
  assert(sslot->is_client);

  // Handle reordering
  assert(pkthdr->req_num <= sslot->cur_req_num);
  bool in_order = (pkthdr->req_num == sslot->cur_req_num) &&
                  (pkthdr->pkt_num == sslot->client_info.resp_rcvd);

  if (likely(in_order)) {
    // pkt_num == resp_rcvd means that we haven't received the full response
    // before now, so the request MsgBuffer (tx_msgbuf) is valid.
    assert(sslot->tx_msgbuf != nullptr &&
           sslot->tx_msgbuf->is_dynamic_and_matches(pkthdr));

    // Check if the response has been reordered before a credit return.
    in_order &=
        (sslot->client_info.expl_cr_rcvd == sslot->tx_msgbuf->num_pkts - 1);

    // When we roll back req_sent during packet loss recovery, for instance
    // from 8 to 7 for an 8-packet request, we can get response packet 0 before
    // the event loop re-sends the 8th request packet. This received response
    // packet is out-of-order.
    in_order &= (sslot->client_info.req_sent == sslot->tx_msgbuf->num_pkts);

    // When we roll back rfr_sent during packet loss recovery, for instance from
    // 8 to 0, we can get response packets 1--8 before the event loop re-sends
    // the RFR packets. These received packets are out of order.
    in_order &= (pkthdr->pkt_num <= sslot->client_info.rfr_sent);
  }

  if (unlikely(!in_order)) {
    LOG_DEBUG(
        "eRPC Rpc %u: Received out-of-order response for session %u. "
        "Req/pkt numbers: %zu/%zu (pkt), %zu/%zu (sslot). Dropping.\n",
        rpc_id, sslot->session->local_session_num, pkthdr->req_num,
        pkthdr->pkt_num, sslot->cur_req_num, sslot->client_info.resp_rcvd);
    return;
  }

  bump_credits(sslot->session);
  if (kCC) {
    size_t rtt_tsc =
        dpath_rdtsc() -
        sslot->client_info.tx_ts[pkthdr->pkt_num % kSessionCredits];
    sslot->session->client_info.cc.timely.update_rate(rtt_tsc);
  }

  MsgBuffer *resp_msgbuf = sslot->client_info.resp_msgbuf;
  if (pkthdr->pkt_num == 0) {
    // This is the first response packet, so resize the response MsgBuffer
    assert(resp_msgbuf->max_data_size >= pkthdr->msg_size);
    resize_msg_buffer(resp_msgbuf, pkthdr->msg_size);
    *(resp_msgbuf->get_pkthdr_0()) = *pkthdr;  // Copy packet header

    sslot->client_info.resp_rcvd = 1;
  } else {
    // We've already resized resp msgbuf and copied pkthdr_0
    assert(resp_msgbuf->is_dynamic_and_matches(pkthdr));
    assert(sslot->client_info.resp_rcvd >= 1);

    sslot->client_info.resp_rcvd++;
  }

  size_t &rfr_sent = sslot->client_info.rfr_sent;

  // Check if we need to send more request-for-response packets
  size_t rfr_pending = ((resp_msgbuf->num_pkts - 1) - rfr_sent);
  if (rfr_pending > 0) {
    size_t now_sending =
        std::min(sslot->session->client_info.credits, rfr_pending);
    assert(now_sending > 0);

    for (size_t i = 0; i < now_sending; i++) {
      enqueue_rfr_st(sslot, pkthdr);
      rfr_sent++;
      assert(rfr_sent <= resp_msgbuf->num_pkts - 1);
    }

    sslot->session->client_info.credits -= now_sending;
  }

  // Header 0 was copied earlier, other headers are unneeded, so copy just data
  copy_data_to_msgbuf(resp_msgbuf, pkthdr);

  if (sslot->client_info.resp_rcvd != resp_msgbuf->num_pkts) return;

  sslot->tx_msgbuf = nullptr;  // Mark response as received
  if (sslot->client_info.cont_etid == kInvalidBgETid) {
    sslot->client_info.cont_func(static_cast<RespHandle *>(sslot), context,
                                 sslot->client_info.tag);
  } else {
    submit_background_st(sslot, Nexus::BgWorkItemType::kResp,
                         sslot->client_info.cont_etid);
  }
  return;
}
}  // End erpc

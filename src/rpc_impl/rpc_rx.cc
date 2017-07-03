#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::process_comps_st() {
  assert(in_creator());
  size_t num_pkts = transport->rx_burst();
  if (num_pkts == 0) return;

  for (size_t i = 0; i < num_pkts; i++) {
    uint8_t *pkt = rx_ring[rx_ring_head];
    rx_ring_head = mod_add_one<Transport::kRecvQueueDepth>(rx_ring_head);

    const pkthdr_t *pkthdr = reinterpret_cast<pkthdr_t *>(pkt);
    assert(pkthdr->check_magic());
    assert(pkthdr->msg_size <= kMaxMsgSize);  // msg_size can be 0 here

    uint16_t session_num = pkthdr->dest_session_num;  // The local session
    assert(session_num < session_vec.size());

    Session *session = session_vec[session_num];
    if (unlikely(session == nullptr)) {
      erpc_dprintf(
          "eRPC Rpc %u: Warning: Received packet %s for buried session. "
          "Dropping packet.\n",
          rpc_id, pkthdr->to_string().c_str());
      continue;
    }

    if (unlikely(!session->is_connected())) {
      erpc_dprintf(
          "eRPC Rpc %u: Warning: Received packet %s for unconnected "
          "session (state is %s). Dropping packet.\n",
          rpc_id, pkthdr->to_string().c_str(),
          session_state_str(session->state).c_str());
      continue;
    }

    // If we are here, we have a valid packet for a connected session
    dpath_dprintf("eRPC Rpc %u: Received packet %s.\n", rpc_id,
                  pkthdr->to_string().c_str());

    // Locate the session slot
    size_t sslot_i = pkthdr->req_num % Session::kSessionReqWindow;  // Bit shift
    SSlot *sslot = &session->sslot_arr[sslot_i];

    // Process control packets, which are sent only for large RPCs
    if (small_rpc_unlikely(pkthdr->msg_size == 0)) {
      assert(pkthdr->is_expl_cr() || pkthdr->is_req_for_resp());
      if (pkthdr->is_expl_cr()) {
        process_expl_cr_st(sslot, pkthdr);
      } else {
        process_req_for_resp_st(sslot, pkthdr);
      }
      continue;
    }

    // If we're here, this is a data packet
    assert(pkthdr->is_req() || pkthdr->is_resp());

    if (small_rpc_likely(pkthdr->msg_size <= TTr::kMaxDataPerPkt)) {
      assert(pkthdr->pkt_num == 0);
      if (pkthdr->is_req()) {
        process_small_req_st(sslot, pkt);
      } else {
        process_small_resp_st(sslot, pkt);
      }
    } else {
      if (pkthdr->is_req()) {
        process_large_req_one_st(sslot, pkt);
      } else {
        process_large_resp_one_st(sslot, pkt);
      }
    }
  }

  // Technically, these RECVs can be posted immediately after rx_burst(), or
  // even in the rx_burst() code.
  transport->post_recvs(num_pkts);
}

template <class TTr>
void Rpc<TTr>::process_small_req_st(SSlot *sslot, const uint8_t *pkt) {
  assert(in_creator());
  assert(sslot != nullptr);
  assert(pkt != nullptr);

  const pkthdr_t *pkthdr = reinterpret_cast<const pkthdr_t *>(pkt);

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
      erpc_dprintf("%s: Dropping.\n", issue_msg);
      return;
    } else {
      // This is a retransmission for the currently active request
      assert(sslot->server_info.req_rcvd == 1);

      if (sslot->tx_msgbuf != nullptr) {
        // The response is available, so resend it
        assert(sslot->tx_msgbuf->get_req_num() == sslot->cur_req_num);

        erpc_dprintf("%s: Re-sending response.\n", issue_msg);
        enqueue_pkt_tx_burst_st(sslot, 0, std::min(sslot->tx_msgbuf->data_size,
                                                   TTr::kMaxDataPerPkt));
        return;
      } else {
        // The response is not available yet, client will have to timeout again
        erpc_dprintf("%s: Dropping because response not available yet.\n",
                     issue_msg);
        return;
      }
    }
  }

  // If we're here, this is the first (and only) packet of the next request
  assert(pkthdr->req_num == sslot->cur_req_num + Session::kSessionReqWindow);

  // The previous request MsgBuffer was buried when its handler returned
  auto &req_msgbuf = sslot->server_info.req_msgbuf;
  assert(req_msgbuf.is_buried());

  // Update sslot tracking
  sslot->cur_req_num = pkthdr->req_num;
  sslot->server_info.req_rcvd = 1;
  bury_tx_msgbuf_server(sslot);  // Bury the previous possibly-dynamic response

  const ReqFunc &req_func = req_func_arr[pkthdr->req_type];
  if (unlikely(!req_func.is_registered())) {
    erpc_dprintf(
        "eRPC Rpc %u: Warning: Received packet for unknown request type %zu. "
        "Dropping packet.\n",
        rpc_id, pkthdr->req_type);
    return;
  }

  // Remember request metadata for enqueue_response()
  sslot->server_info.req_func_type = req_func.req_func_type;
  sslot->server_info.req_type = pkthdr->req_type;

  if (small_rpc_likely(!req_func.is_background())) {
    // For foreground request handlers, a "fake" request MsgBuffer suffices --
    // it's valid for the duration of req_func().
    req_msgbuf = MsgBuffer(pkt, pkthdr->msg_size);
    req_func.req_func(static_cast<ReqHandle *>(sslot), context);
    bury_rx_msgbuf_nofree(sslot);
    return;
  } else {
    // For background request handlers, we need a RX ring--independent copy of
    // the request. The allocated req_msgbuf is freed by the background thread.
    req_msgbuf = alloc_msg_buffer(pkthdr->msg_size);
    assert(req_msgbuf.buf != nullptr);
    memcpy(reinterpret_cast<char *>(req_msgbuf.get_pkthdr_0()), pkt,
           pkthdr->msg_size + sizeof(pkthdr_t));
    submit_background_st(sslot, Nexus<TTr>::BgWorkItemType::kReq);
    return;
  }
}

template <class TTr>
void Rpc<TTr>::process_small_resp_st(SSlot *sslot, const uint8_t *pkt) {
  assert(in_creator());
  assert(sslot != nullptr);
  assert(pkt != nullptr);

  const pkthdr_t *pkthdr = reinterpret_cast<const pkthdr_t *>(pkt);

  // Handle reordering
  assert(pkthdr->req_num <= sslot->cur_req_num);
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
    erpc_dprintf(
        "eRPC Rpc %u: Received out-of-order response for session %u. "
        "Request num: %zu (pkt), %zu (sslot). Dropping.\n",
        rpc_id, sslot->session->local_session_num, pkthdr->req_num,
        sslot->cur_req_num);
    return;
  }

  // If we're here, this is the first (and only) packet of the response
  assert(sslot->tx_msgbuf != nullptr &&  // Check the request MsgBuffer
         sslot->tx_msgbuf->is_dynamic_and_matches(pkthdr));

  // Check that the app's response MsgBuffer has sufficient space, and resize it
  MsgBuffer *resp_msgbuf = sslot->client_info.resp_msgbuf;
  assert(resp_msgbuf != nullptr);
  assert(resp_msgbuf->max_data_size >= pkthdr->msg_size);
  resize_msg_buffer(resp_msgbuf, pkthdr->msg_size);

  sslot->client_info.resp_rcvd = 1;
  bump_credits(sslot->session);

  // Bury the request MsgBuffer (tx_msgbuf) without freeing user-owned memory.
  // This also records that the full response has been received.
  bury_tx_msgbuf_client(sslot);

  // Copy the header and data
  memcpy(reinterpret_cast<char *>(resp_msgbuf->get_pkthdr_0()),
         reinterpret_cast<const char *>(pkt),
         pkthdr->msg_size + sizeof(pkthdr_t));

  if (small_rpc_likely(sslot->client_info.cont_etid == kInvalidBgETid)) {
    sslot->client_info.cont_func(static_cast<RespHandle *>(sslot), context,
                                 sslot->client_info.tag);
  } else {
    // Background thread will run continuation
    submit_background_st(sslot, Nexus<TTr>::BgWorkItemType::kResp,
                         sslot->client_info.cont_etid);
    return;
  }
}

template <class TTr>
void Rpc<TTr>::process_expl_cr_st(SSlot *sslot, const pkthdr_t *pkthdr) {
  assert(in_creator());
  assert(sslot != nullptr);
  assert(pkthdr != nullptr);

  // Handle reordering
  assert(pkthdr->req_num <= sslot->cur_req_num);
  bool in_order = (pkthdr->req_num == sslot->cur_req_num) &&
                  (pkthdr->pkt_num == sslot->client_info.expl_cr_rcvd);

  // When we roll back req_sent during packet loss recovery, for instance from 8
  // to 0, we can get credit returns for request packets 0--7 before the event
  // loop re-sends the request packets. These received packets are out of order.
  in_order &= (pkthdr->pkt_num < sslot->client_info.req_sent);

  if (unlikely(!in_order)) {
    erpc_dprintf(
        "eRPC Rpc %u: Received out-of-order explicit CR for session %u. "
        "Pkt = %zu/%zu. cur_req_num = %zu, expl_cr_rcvd = %zu. Dropping.\n",
        rpc_id, sslot->session->local_session_num, pkthdr->req_num,
        pkthdr->pkt_num, sslot->cur_req_num, sslot->client_info.expl_cr_rcvd);
    return;
  }

  sslot->client_info.expl_cr_rcvd++;
  bump_credits(sslot->session);
}

template <class TTr>
void Rpc<TTr>::process_req_for_resp_st(SSlot *sslot, const pkthdr_t *pkthdr) {
  assert(in_creator());
  assert(sslot != nullptr);
  assert(pkthdr != nullptr);

  // Handle reordering
  assert(pkthdr->req_num <= sslot->cur_req_num);
  bool in_order = (pkthdr->req_num == sslot->cur_req_num) &&
                  (pkthdr->pkt_num == sslot->server_info.rfr_rcvd + 1);
  if (unlikely(!in_order)) {
    char issue_msg[kMaxIssueMsgLen];
    sprintf(issue_msg,
            "eRPC Rpc %u: Received out-of-order RFR for session %u. "
            "Pkt = %zu/%zu. cur_req_num = %zu, rfr_rcvd = %zu. Action",
            rpc_id, sslot->session->local_session_num, pkthdr->req_num,
            pkthdr->pkt_num, sslot->cur_req_num, sslot->server_info.rfr_rcvd);

    if (pkthdr->req_num < sslot->cur_req_num) {
      // Reject RFR for old requests
      erpc_dprintf("%s: Dropping.\n", issue_msg);
      return;
    }

    if (pkthdr->pkt_num > sslot->server_info.rfr_rcvd + 1) {
      // Reject future packets
      erpc_dprintf("%s: Dropping.\n", issue_msg);
      return;
    }

    // If we're here, this is a past RFR packet for this request. Resend resp.
    assert(pkthdr->req_num == sslot->cur_req_num &&
           pkthdr->pkt_num < sslot->server_info.rfr_rcvd + 1);
    erpc_dprintf("%s: Re-sending response.\n", issue_msg);

    // Send the response packet with index = pkthdr->pkt_num (same as below)
    size_t offset = pkthdr->pkt_num * TTr::kMaxDataPerPkt;
    assert(offset < sslot->tx_msgbuf->data_size);
    size_t data_bytes =
        std::min(TTr::kMaxDataPerPkt, sslot->tx_msgbuf->data_size - offset);
    enqueue_pkt_tx_burst_st(sslot, offset, data_bytes);
    return;
  }

  sslot->server_info.rfr_rcvd++;

  // Send the response packet with index = pkthdr->pktnum (same as above)
  size_t offset = pkthdr->pkt_num * TTr::kMaxDataPerPkt;
  assert(offset < sslot->tx_msgbuf->data_size);
  size_t data_bytes =
      std::min(TTr::kMaxDataPerPkt, sslot->tx_msgbuf->data_size - offset);
  enqueue_pkt_tx_burst_st(sslot, offset, data_bytes);
}

// This function is for large messages, so don't use small_rpc_likely()
template <class TTr>
void Rpc<TTr>::process_large_req_one_st(SSlot *sslot, const uint8_t *pkt) {
  assert(in_creator());
  assert(sslot != nullptr);
  assert(pkt != nullptr);

  const pkthdr_t *pkthdr = reinterpret_cast<const pkthdr_t *>(pkt);
  MsgBuffer &req_msgbuf = sslot->server_info.req_msgbuf;

  // Handle reordering
  bool is_next_pkt_same_req =  // Is this the next packet in this request?
      (pkthdr->req_num == sslot->cur_req_num) &&
      (pkthdr->pkt_num == sslot->server_info.req_rcvd);
  bool is_first_pkt_next_req =  // Is this the first packet in the next request?
      (pkthdr->req_num == sslot->cur_req_num + Session::kSessionReqWindow) &&
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
    if ((pkthdr->req_num != sslot->cur_req_num) ||
        (pkthdr->pkt_num > sslot->server_info.req_rcvd)) {
      erpc_dprintf("%s: Dropping.\n", issue_msg);
      return;
    }

    // rx_msgbuf could be buried if we've received the entire request and
    // processed it, so directly compute the number of packets in the request
    size_t num_pkts_in_req = TTr::data_size_to_num_pkts(pkthdr->msg_size);
    if (sslot->server_info.req_rcvd != num_pkts_in_req) {
      assert(req_msgbuf.is_dynamic_and_matches(pkthdr));
    }

    if (pkthdr->pkt_num != num_pkts_in_req - 1) {
      // This is not the last packet in the request => send a credit return
      erpc_dprintf("%s: Re-sending credit return.\n", issue_msg);
      send_credit_return_now_st(sslot->session, pkthdr);
      return;
    }

    // This is the last request packet, so re-send response if it's available
    if (sslot->tx_msgbuf != nullptr) {
      // The response is available, so resend it
      assert(sslot->tx_msgbuf->get_req_num() == sslot->cur_req_num);

      erpc_dprintf("%s: Re-sending response.\n", issue_msg);
      enqueue_pkt_tx_burst_st(
          sslot, 0, std::min(sslot->tx_msgbuf->data_size, TTr::kMaxDataPerPkt));
    } else {
      // The response is not available yet, client will have to timeout again
      erpc_dprintf("%s: Dropping because response not available yet.\n",
                   issue_msg);
    }
    return;
  }

  // Allocate or locate the request MsgBuffer
  if (pkthdr->pkt_num == 0) {
    // This is the first packet received for this request
    assert(req_msgbuf.is_buried());  // Buried earlier when req handler returned

    // Update sslot tracking
    sslot->cur_req_num = pkthdr->req_num;
    sslot->server_info.req_rcvd = 1;
    bury_tx_msgbuf_server(sslot);  // Bury previous possibly-dynamic response

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
  if (pkthdr->pkt_num != req_msgbuf.num_pkts - 1) {
    send_credit_return_now_st(sslot->session, pkthdr);
  }

  copy_to_msgbuf(&req_msgbuf, pkt);  // Copy data (header 0 was copied earlier)

  // Invoke the request handler iff we have all the request packets
  if (sslot->server_info.req_rcvd != req_msgbuf.num_pkts) {
    return;
  }

  const ReqFunc &req_func = req_func_arr[pkthdr->req_type];
  if (unlikely(!req_func.is_registered())) {
    erpc_dprintf(
        "eRPC Rpc %u: Warning: Received packet for unknown request type %zu. "
        "Dropping packet.\n",
        rpc_id, pkthdr->req_type);
    return;
  }

  // Remember request metadata for enqueue_response()
  sslot->server_info.req_func_type = req_func.req_func_type;
  sslot->server_info.req_type = pkthdr->req_type;

  // rx_msgbuf here is independent of the RX ring, so we never need a copy
  if (!req_func.is_background()) {
    req_func.req_func(static_cast<ReqHandle *>(sslot), context);
    bury_req_msgbuf(sslot);
  } else {
    submit_background_st(sslot, Nexus<TTr>::BgWorkItemType::kReq);
  }
}

// This function is for large messages, so don't use small_rpc_likely()
template <class TTr>
void Rpc<TTr>::process_large_resp_one_st(SSlot *sslot, const uint8_t *pkt) {
  assert(in_creator());
  assert(sslot != nullptr);
  assert(pkt != nullptr);

  const pkthdr_t *pkthdr = reinterpret_cast<const pkthdr_t *>(pkt);

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
    erpc_dprintf(
        "eRPC Rpc %u: Received out-of-order response for session %u. "
        "Req/pkt numbers: %zu/%zu (pkt), %zu/%zu (sslot). Dropping.\n",
        rpc_id, sslot->session->local_session_num, pkthdr->req_num,
        pkthdr->pkt_num, sslot->cur_req_num, sslot->client_info.resp_rcvd);
    return;
  }

  bump_credits(sslot->session);

  // Allocate or locate the response MsgBuffer
  MsgBuffer *resp_msgbuf = sslot->client_info.resp_msgbuf;
  if (pkthdr->pkt_num == 0) {
    // This is the first packet received for this response
    resize_msg_buffer(resp_msgbuf, pkthdr->msg_size);
    *(resp_msgbuf->get_pkthdr_0()) = *pkthdr;  // Copy packet header XXX

    sslot->client_info.resp_rcvd = 1;
  } else {
    // This is not the first packet for this request
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
      send_req_for_resp_now_st(sslot, pkthdr);
      rfr_sent++;
      assert(rfr_sent <= resp_msgbuf->num_pkts - 1);
    }

    sslot->session->client_info.credits -= now_sending;
  }

  copy_to_msgbuf(sslot->client_info.resp_msgbuf, pkt);  // Copy only data

  // Invoke the continuation iff we have all the response packets
  if (sslot->client_info.resp_rcvd != resp_msgbuf->num_pkts) {
    return;
  }

  // Bury the request MsgBuffer (tx_msgbuf) without freeing user-owned memory
  // This also records that the full response has been received.
  bury_tx_msgbuf_client(sslot);

  if (small_rpc_likely(sslot->client_info.cont_etid == kInvalidBgETid)) {
    sslot->client_info.cont_func(static_cast<RespHandle *>(sslot), context,
                                 sslot->client_info.tag);
  } else {
    submit_background_st(sslot, Nexus<TTr>::BgWorkItemType::kResp,
                         sslot->client_info.cont_etid);
  }
  return;
}

template <class TTr>
void Rpc<TTr>::submit_background_st(SSlot *sslot,
                                    typename Nexus<TTr>::BgWorkItemType wi_type,
                                    size_t bg_etid) {
  assert(in_creator());
  assert(sslot != nullptr);
  assert(bg_etid < nexus->num_bg_threads || bg_etid == kInvalidBgETid);
  assert(nexus->num_bg_threads > 0);

  // Sanity-check RX and TX MsgBuffers
  debug_check_bg_rx_msgbuf(sslot, wi_type);
  assert(sslot->tx_msgbuf == nullptr);

  if (bg_etid == kInvalidBgETid) {
    // Background thread was not specified, so choose one at random
    bg_etid = fast_rand.next_u32() % nexus->num_bg_threads;
  }

  auto *req_list = nexus_hook.bg_req_list_arr[bg_etid];

  // Thread-safe
  req_list->unlocked_push_back(
      typename Nexus<TTr>::BgWorkItem(wi_type, this, context, sslot));
}

// This is a debug function that gets optimized out
template <class TTr>
void Rpc<TTr>::debug_check_bg_rx_msgbuf(
    SSlot *sslot, typename Nexus<TTr>::BgWorkItemType wi_type) {
  assert(sslot != nullptr);
  _unused(sslot);
  _unused(wi_type);

  MsgBuffer *rx_msgbuf = sslot->is_client ? sslot->client_info.resp_msgbuf
                                          : &sslot->server_info.req_msgbuf;
  _unused(rx_msgbuf);

  assert(rx_msgbuf->buf != nullptr && rx_msgbuf->check_magic());
  assert(rx_msgbuf->is_dynamic());

  if (wi_type == Nexus<TTr>::BgWorkItemType::kReq) {
    assert(rx_msgbuf->is_req());
  } else {
    assert(rx_msgbuf->is_resp());
  }
}

}  // End ERpc

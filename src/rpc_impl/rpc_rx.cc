#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::process_comps_st() {
  assert(in_creator());
  size_t num_pkts = transport->rx_burst();
  if (num_pkts == 0) {
    return;
  }

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
            "eRPC Rpc %u: Received out-of-order request packet. "
            "Request numbers: %zu (packet), %zu (sslot). Action:",
            rpc_id, pkthdr->req_num, sslot->cur_req_num);

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
        enqueue_pkt_tx_burst_st(sslot, 0, sslot->tx_msgbuf->data_size);
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
  assert(sslot->rx_msgbuf.is_buried());

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
  sslot->server_info.req_num = pkthdr->req_num;

  if (small_rpc_likely(!req_func.is_background())) {
    // For foreground (terminal/non-terminal) request handlers, a "fake",
    // MsgBuffer suffices (i.e., it's valid for the duration of req_func).
    sslot->rx_msgbuf = MsgBuffer(pkt, pkthdr->msg_size);
    req_func.req_func(static_cast<ReqHandle *>(sslot), context);
    bury_rx_msgbuf_nofree(sslot);
    return;
  } else {
    // For background request handlers, we need a RX ring--independent copy of
    // the request. The allocated rx_msgbuf is freed by the background thread.
    sslot->rx_msgbuf = alloc_msg_buffer(pkthdr->msg_size);
    assert(sslot->rx_msgbuf.buf != nullptr);
    memcpy(reinterpret_cast<char *>(sslot->rx_msgbuf.get_pkthdr_0()), pkt,
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
  if (unlikely(pkthdr->req_num < sslot->cur_req_num)) {
    erpc_dprintf(
        "eRPC Rpc %u: Received out-of-order response packet. "
        "Request numbers: %zu (packet), %zu (sslot). Dropping.\n",
        rpc_id, pkthdr->req_num, sslot->cur_req_num);
    return;
  }

  // If we're here, this is the first (and only) packet of the response
  assert(sslot->rx_msgbuf.is_buried());  // Older rx_msgbuf was buried earlier
  assert(sslot->tx_msgbuf != nullptr &&  // Check the request MsgBuffer
         sslot->tx_msgbuf->is_dynamic_and_matches(pkthdr));

  sslot->client_info.resp_rcvd = 1;
  bump_credits(sslot->session);

  // Bury the request MsgBuffer (tx_msgbuf) without freeing user-owned memory.
  // This also records that the full response has been received.
  bury_tx_msgbuf_client(sslot);

  if (small_rpc_likely(sslot->client_info.cont_etid == kInvalidBgETid)) {
    // Continuation will run in foreground with a "fake" static MsgBuffer
    sslot->rx_msgbuf = MsgBuffer(pkt, pkthdr->msg_size);
    sslot->client_info.cont_func(static_cast<RespHandle *>(sslot), context,
                                 sslot->client_info.tag);

    // The continuation must release the response (rx_msgbuf), and only the
    // event loop (this thread) can re-use it and make it non-NULL.
    assert(sslot->rx_msgbuf.buf == nullptr);
  } else {
    // For background continuations, we need a RX ring--independent copy of
    // the resp. The allocated rx_msgbuf is freed by the background thread.
    sslot->rx_msgbuf = alloc_msg_buffer(pkthdr->msg_size);
    assert(sslot->rx_msgbuf.buf != nullptr);
    memcpy(reinterpret_cast<char *>(sslot->rx_msgbuf.get_pkthdr_0()), pkt,
           pkthdr->msg_size + sizeof(pkthdr_t));
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
  if (unlikely(pkthdr->req_num < sslot->cur_req_num)) {
    // Reject credit returns for old requests
    erpc_dprintf(
        "eRPC Rpc %u: Received out-of-order explicit credit return packet. "
        "Request numbers: %zu (packet), %zu (sslot). Dropping.\n",
        rpc_id, pkthdr->req_num, sslot->cur_req_num);
    return;
  } else {
    // Reject credit returns for previous and later packets
    if (unlikely(pkthdr->pkt_num != sslot->client_info.cr_rcvd)) {
      erpc_dprintf(
          "eRPC Rpc %u: Received out-of-order explicit credit return packet. "
          "Packet numbers: %zu (packet), %zu (expected). Dropping.\n",
          rpc_id, pkthdr->pkt_num, sslot->client_info.cr_rcvd);
      return;
    }
  }

  sslot->client_info.cr_rcvd++;
  bump_credits(sslot->session);
}

template <class TTr>
void Rpc<TTr>::process_req_for_resp_st(SSlot *sslot, const pkthdr_t *pkthdr) {
  assert(in_creator());
  assert(sslot != nullptr);
  assert(pkthdr != nullptr);

  // Handle reordering
  assert(pkthdr->req_num <= sslot->cur_req_num);
  if (unlikely(pkthdr->req_num < sslot->cur_req_num)) {
    // Reject RFR for old requests
    erpc_dprintf(
        "eRPC Rpc %u: Received out-of-order request-for-response packet. "
        "Request numbers: %zu (packet), %zu (sslot). Dropping.\n",
        rpc_id, pkthdr->req_num, sslot->cur_req_num);
    return;
  } else {
    // The expected packet number is (rfr_rcvd + 1). Reject future packets.
    if (unlikely(pkthdr->pkt_num > sslot->server_info.rfr_rcvd + 1)) {
      erpc_dprintf(
          "eRPC Rpc %u: Received out-of-order credit return packet. "
          "Packet numbers: %zu (packet), %zu (expected). Dropping.\n",
          rpc_id, pkthdr->pkt_num, sslot->server_info.rfr_rcvd + 1);
      return;
    }

    // Re-send RFR response for older packets
    if (unlikely(pkthdr->pkt_num < sslot->server_info.rfr_rcvd + 1)) {
      erpc_dprintf(
          "eRPC Rpc %u: Received out-of-order credit return packet. "
          "Packet numbers: %zu (packet), %zu (expected). "
          "Re-sending response packet.\n",
          rpc_id, pkthdr->pkt_num, sslot->server_info.rfr_rcvd + 1);

      // Send the response packet with index = pkthdr->pkt_num (same as below)
      size_t offset = pkthdr->pkt_num * TTr::kMaxDataPerPkt;
      assert(offset < sslot->tx_msgbuf->data_size);
      size_t data_bytes =
          std::min(TTr::kMaxDataPerPkt, sslot->tx_msgbuf->data_size - offset);
      enqueue_pkt_tx_burst_st(sslot, offset, data_bytes);

      return;
    }
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

  // Handle reordering
  bool is_next_pkt_same_req =  // Is this the next packet in this request?
      (pkthdr->req_num == sslot->cur_req_num) &&
      (pkthdr->pkt_num == sslot->server_info.req_rcvd);
  bool is_first_pkt_next_req = // Is this the first packet in the next request?
      (pkthdr->req_num == sslot->cur_req_num + Session::kSessionReqWindow) &&
      (pkthdr->pkt_num == 0);

  bool is_ordered = is_next_pkt_same_req || is_first_pkt_next_req;
  if (unlikely(!is_ordered)) {
    char issue_msg[kMaxIssueMsgLen];
    sprintf(issue_msg,
            "eRPC Rpc %u: Received out-of-order request packet. "
            "Req/pkt numbers: %zu/%zu (packet), %zu/%zu (sslot). Action:",
            rpc_id, pkthdr->req_num, pkthdr->pkt_num, sslot->cur_req_num,
            sslot->server_info.req_rcvd);

    if (pkthdr->req_num < sslot->cur_req_num) {
      // This is a massively-delayed retransmission of an old request
      erpc_dprintf("%s: Dropping.\n", issue_msg);
      return;
    } else if (pkthdr->req_num == sslot->cur_req_num) {
      // This is an out-of-order packet for the currently active request
      if (pkthdr->pkt_num > sslot->server_info.req_rcvd) {
        // Drop future request packets
        erpc_dprintf("%s: Dropping.\n", issue_msg);
        return;
      }

      // If we're here, we've received this packet before
      assert(sslot->rx_msgbuf.is_dynamic_and_matches(pkthdr));

      if (pkthdr->pkt_num != sslot->rx_msgbuf.num_pkts - 1) {
        // This is not the last packet so we just send a credit return
        erpc_dprintf("%s: Re-sending credit return.\n", issue_msg);
        send_credit_return_now_st(sslot->session, pkthdr);
        return;
      }

      // If we're here, this is the last request packet, so try to resend resp
      if (sslot->tx_msgbuf != nullptr) {
        // The response is available, so resend it
        assert(sslot->tx_msgbuf->get_req_num() == sslot->cur_req_num);

        erpc_dprintf("%s: Re-sending response.\n", issue_msg);
        enqueue_pkt_tx_burst_st(sslot, 0, sslot->tx_msgbuf->data_size);
        return;
      } else {
        // The response is not available yet, client will have to timeout again
        erpc_dprintf("%s: Dropping because response not available yet.\n",
                     issue_msg);
        return;
      }
    } else {
      // This is an out-of-order packet of the next request
      erpc_dprintf("%s: Dropping.\n", issue_msg);
      return;
    }
  }

  // Allocate or locate the RX MsgBuffer for this message
  MsgBuffer &rx_msgbuf = sslot->rx_msgbuf;
  if (pkthdr->pkt_num == 0) {
    // This is the first time that we have received a packet for this message.
    assert(rx_msgbuf.is_buried());  // Buried earlier
    bury_tx_msgbuf_server(sslot);   // Bury the previous response MsgBuffer

    rx_msgbuf = alloc_msg_buffer(pkthdr->msg_size);
    assert(sslot->rx_msgbuf.buf != nullptr);
    *(rx_msgbuf.get_pkthdr_0()) = *pkthdr;  // Copy packet header

    sslot->server_info.req_rcvd = 1;
    sslot->cur_req_num = pkthdr->req_num;  // XXX: Reordering
  } else {
    // We already have a valid, dynamically-allocated RX MsgBuffer
    assert(rx_msgbuf.is_dynamic_and_matches(pkthdr));
    assert(sslot->server_info.req_rcvd >= 1);
    sslot->server_info.req_rcvd++;
  }

  // Send a credit return for every request packet except the last in sequence
  if (pkthdr->pkt_num != rx_msgbuf.num_pkts - 1) {
    send_credit_return_now_st(sslot->session, pkthdr);
  }

  copy_to_rx_msgbuf(sslot, pkt);  // Copy data (header 0 was copied earlier)

  // Invoke the request handler iff we have all the request packets
  if (sslot->server_info.req_rcvd != rx_msgbuf.num_pkts) {
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
  sslot->server_info.req_num = pkthdr->req_num;

  // rx_msgbuf here is independent of the RX ring, so we never need a copy
  if (!req_func.is_background()) {
    req_func.req_func(static_cast<ReqHandle *>(sslot), context);
    bury_rx_msgbuf(sslot);
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

  // XXX: Handle reordering

  bump_credits(sslot->session);

  // Allocate or locate the RX MsgBuffer for this message
  MsgBuffer &rx_msgbuf = sslot->rx_msgbuf;
  if (pkthdr->pkt_num == 0) {
    // This is the first time that we have received a packet for this message.
    assert(rx_msgbuf.is_buried());  // Buried earlier

    rx_msgbuf = alloc_msg_buffer(pkthdr->msg_size);
    assert(sslot->rx_msgbuf.buf != nullptr);
    *(rx_msgbuf.get_pkthdr_0()) = *pkthdr;  // Copy packet header

    sslot->client_info.resp_rcvd = 1;
  } else {
    // We already have a valid, dynamically-allocated RX MsgBuffer.
    assert(rx_msgbuf.is_dynamic_and_matches(pkthdr));
    assert(sslot->client_info.resp_rcvd >= 1);

    sslot->client_info.resp_rcvd++;
  }

  assert(sslot->tx_msgbuf != nullptr &&  // Check the request MsgBuffer
         sslot->tx_msgbuf->is_dynamic_and_matches(pkthdr));

  size_t &rfr_sent = sslot->client_info.rfr_sent;

  // Check if we need to send more request-for-response packets
  size_t rfr_pending = ((rx_msgbuf.num_pkts - 1) - rfr_sent);
  if (rfr_pending > 0) {
    size_t now_sending =
        std::min(sslot->session->client_info.credits, rfr_pending);
    assert(now_sending > 0);

    for (size_t i = 0; i < now_sending; i++) {
      // This doesn't use pkthdr->pkt_num: it uses sslot's rfr_sent
      send_req_for_resp_now_st(sslot, pkthdr);
      rfr_sent++;
      assert(rfr_sent <= rx_msgbuf.num_pkts - 1);
    }

    sslot->session->client_info.credits -= now_sending;
  }

  copy_to_rx_msgbuf(sslot, pkt);  // Copy data (header 0 was copied earlier)

  // Invoke the continuation iff we have all the response packets
  if (sslot->client_info.resp_rcvd != rx_msgbuf.num_pkts) {
    return;
  }

  // Bury the request MsgBuffer (tx_msgbuf) without freeing user-owned memory
  // This also records that the full response has been received.
  bury_tx_msgbuf_client(sslot);

  if (small_rpc_likely(sslot->client_info.cont_etid == kInvalidBgETid)) {
    sslot->client_info.cont_func(static_cast<RespHandle *>(sslot), context,
                                 sslot->client_info.tag);

    // The continuation must release the response (rx_msgbuf), and only the
    // event loop (this thread) can un-bury it.
    assert(sslot->rx_msgbuf.buf == nullptr);
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

  assert(sslot->rx_msgbuf.buf != nullptr && sslot->rx_msgbuf.check_magic());
  assert(sslot->rx_msgbuf.is_dynamic());

  if (wi_type == Nexus<TTr>::BgWorkItemType::kReq) {
    assert(sslot->rx_msgbuf.is_req());
  } else {
    assert(sslot->rx_msgbuf.is_resp());
  }
}

}  // End ERpc

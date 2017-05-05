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
      if (pkthdr->is_req()) {
        process_small_req_st(sslot, pkt);
      } else {
        process_small_resp_st(sslot, pkt);
      }
    } else {
      process_comps_large_msg_one_st(sslot, pkt);
    }
  }

  // Technically, these RECVs can be posted immediately after rx_burst(), or
  // even in the rx_burst() code.
  transport->post_recvs(num_pkts);
}

template <class TTr>
void Rpc<TTr>::process_expl_cr_st(SSlot *sslot, const pkthdr_t *pkthdr) {
  assert(in_creator());
  assert(sslot != nullptr);
  assert(pkthdr != nullptr);

  // XXX: Handle reordering
  bump_credits(sslot->session);
}

template <class TTr>
void Rpc<TTr>::process_req_for_resp_st(SSlot *sslot, const pkthdr_t *pkthdr) {
  assert(in_creator());
  assert(sslot != nullptr);
  assert(pkthdr != nullptr);

  size_t pkt_num = pkthdr->pkt_num;
  assert(pkt_num < sslot->tx_msgbuf->num_pkts);

  // Send the response packet with index = pkt_num. XXX: Handle reordering.
  size_t offset = pkt_num * TTr::kMaxDataPerPkt;
  assert(offset < sslot->tx_msgbuf->data_size);
  size_t data_bytes =
      std::min(TTr::kMaxDataPerPkt, sslot->tx_msgbuf->data_size - offset);
  enqueue_pkt_tx_burst_st(sslot, offset, data_bytes);
}

template <class TTr>
void Rpc<TTr>::process_small_req_st(SSlot *sslot, const uint8_t *pkt) {
  assert(in_creator());
  assert(sslot != nullptr);
  assert(pkt != nullptr);

  // XXX: Handle reordering
  assert(sslot->rx_msgbuf.is_buried());  // Older rx_msgbuf was buried earlier
  sslot->pkts_rcvd = 1;  // We need this for detecting duplicates

  const pkthdr_t *pkthdr = reinterpret_cast<const pkthdr_t *>(pkt);

  const ReqFunc &req_func = req_func_arr[pkthdr->req_type];
  if (unlikely(!req_func.is_registered())) {
    erpc_dprintf(
        "eRPC Rpc %u: Warning: Received packet for unknown request type %zu. "
        "Dropping packet.\n",
        rpc_id, pkthdr->req_type);
    return;
  }

  // Bury the previous possibly-dynamic response MsgBuffer (tx_msgbuf)
  bury_tx_msgbuf_server(sslot);

  // Remember request metadata for enqueue_response()
  sslot->server_info.req_func_type = req_func.req_func_type;
  sslot->server_info.req_type = pkthdr->req_type;
  sslot->server_info.req_num = pkthdr->req_num;
  if (optlevel_large_rpc_supported) sslot->server_info.rfr_rcvd = 0;

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

  // XXX: Handle reordering
  assert(sslot->rx_msgbuf.is_buried());  // Older rx_msgbuf was buried earlier
  sslot->pkts_rcvd = 1;  // We need this for detecting duplicates

  const pkthdr_t *pkthdr = reinterpret_cast<const pkthdr_t *>(pkt);

  // Handle a single-packet response message
  debug_check_req_msgbuf_on_resp(sslot, pkthdr->req_num, pkthdr->req_type);
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

// This function is for large messages, so don't use small_rpc_likely()
template <class TTr>
void Rpc<TTr>::process_comps_large_msg_one_st(SSlot *sslot,
                                              const uint8_t *pkt) {
  assert(in_creator());
  assert(sslot != nullptr);
  assert(pkt != nullptr);

  // pkt is generally valid. Do some large message-specific checks.
  const pkthdr_t *pkthdr = reinterpret_cast<const pkthdr_t *>(pkt);
  assert(pkthdr->check_magic());
  assert(pkthdr->msg_size > TTr::kMaxDataPerPkt);  // Multi-packet
  assert(pkthdr->is_req() || pkthdr->is_resp());

  // Extract packet header fields
  uint8_t req_type = pkthdr->req_type;
  size_t req_num = pkthdr->req_num;
  size_t msg_size = pkthdr->msg_size;
  size_t pkt_num = pkthdr->pkt_num;
  bool is_req = pkthdr->is_req();

  const ReqFunc &req_func = req_func_arr[req_type];
  if (unlikely(!req_func.is_registered())) {
    erpc_dprintf(
        "eRPC Rpc %u: Warning: Received packet for unknown request type %u. "
        "Dropping packet.\n",
        rpc_id, req_type);
    return;
  }

  // Allocate/locate the RX MsgBuffer for this message if needed
  MsgBuffer &rx_msgbuf = sslot->rx_msgbuf;
  if (pkthdr->pkt_num == 0) {
    // This is the first time that we have received a packet for this message.
    assert(rx_msgbuf.is_buried());  // Buried earlier

    if (pkthdr->is_req()) {
      bury_tx_msgbuf_server(sslot);  // Bury the previous response MsgBuffer
    }

    if (unlikely(pkt_num != 0)) {
      erpc_dprintf(
          "eRPC Rpc %u: Received out-of-order packet on session %u. "
          "Expected packet number = 0, received = %zu.\n",
          rpc_id, sslot->session->local_session_num, pkt_num);
    }

    rx_msgbuf = alloc_msg_buffer(msg_size);
    assert(sslot->rx_msgbuf.buf != nullptr);
    sslot->pkts_rcvd = 1;

    // Store the received packet header into the zeroth rx_msgbuf packet header.
    // It's possible to copy fewer fields, but copying pkthdr_t is cheap.
    *(rx_msgbuf.get_pkthdr_0()) = *pkthdr;
    rx_msgbuf.get_pkthdr_0()->pkt_num = 0;
  } else {
    // This is not the 1st packet received for this message, so we have a valid,
    // dynamically-allocated RX MsgBuffer.
    assert(rx_msgbuf.buf != nullptr);
    assert(rx_msgbuf.is_dynamic() && rx_msgbuf.check_magic());
    assert(rx_msgbuf.get_req_type() == req_type);
    assert(rx_msgbuf.get_req_num() == req_num);

    assert(sslot->pkts_rcvd >= 1);
    sslot->pkts_rcvd++;
  }

  // Manage credits and request-for-response
  if (is_req) {
    // Send a credit return for every request packet except the last in sequence
    if (pkt_num != rx_msgbuf.num_pkts - 1) {
      send_credit_return_now_st(sslot->session, pkthdr);
    }
  } else {
    // This is a response packet
    debug_check_req_msgbuf_on_resp(sslot, req_num, req_type);
    bump_credits(sslot->session);  // We have at least one credit

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
        assert(rfr_sent <= rx_msgbuf.num_pkts - 1);
      }

      sslot->session->client_info.credits -= now_sending;
    }
  }

  // Copy the received packet's data only. The message's common packet header
  // was copied to rx_msgbuf.pkthdr_0 earlier.
  size_t offset = pkt_num * TTr::kMaxDataPerPkt;  // rx_msgbuf offset
  size_t bytes_to_copy = (pkt_num == rx_msgbuf.num_pkts - 1)
                             ? (msg_size - offset)
                             : TTr::kMaxDataPerPkt;
  assert(bytes_to_copy <= TTr::kMaxDataPerPkt);
  memcpy(reinterpret_cast<char *>(&rx_msgbuf.buf[offset]),
         reinterpret_cast<const char *>(pkt + sizeof(pkthdr_t)), bytes_to_copy);

  // Check if we need to invoke the request handler or continuation
  if (sslot->pkts_rcvd != rx_msgbuf.num_pkts) {
    return;
  }

  // If we're here, we received all packets of this message
  if (pkthdr->is_req()) {
    // Remember request metadata for enqueue_response()
    sslot->server_info.req_func_type = req_func.req_func_type;
    sslot->server_info.req_type = req_type;
    sslot->server_info.req_num = req_num;
    if (optlevel_large_rpc_supported) sslot->server_info.rfr_rcvd = 0;

    // rx_msgbuf here is independent of the RX ring, so we never need a copy
    if (!req_func.is_background()) {
      req_func.req_func(static_cast<ReqHandle *>(sslot), context);
      bury_rx_msgbuf(sslot);
      return;
    } else {
      submit_background_st(sslot, Nexus<TTr>::BgWorkItemType::kReq);
      return;
    }
  } else {
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
      return;
    }
    return;
  }
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
void Rpc<TTr>::debug_check_req_msgbuf_on_resp(SSlot *sslot, uint64_t req_num,
                                              uint8_t req_type) {
  assert(sslot != nullptr);
  _unused(sslot);
  _unused(req_num);
  _unused(req_type);

  const MsgBuffer *req_msgbuf = sslot->tx_msgbuf;
  _unused(req_msgbuf);

  assert(req_msgbuf != nullptr && req_msgbuf->buf != nullptr &&
         req_msgbuf->check_magic() && req_msgbuf->is_dynamic());
  assert(req_msgbuf->is_req());
  assert(req_msgbuf->get_req_num() == req_num);
  assert(req_msgbuf->get_req_type() == req_type);
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

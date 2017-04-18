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
      fprintf(stderr,
              "eRPC Rpc %u: Warning: Received packet for buried session %u. "
              "Dropping packet.\n",
              rpc_id, session_num);
      continue;
    }

    if (unlikely(!session->is_connected())) {
      fprintf(stderr,
              "eRPC Rpc %u: Warning: Received packet for unconnected "
              "session %u. Session state is %s. Dropping packet.\n",
              rpc_id, session_num, session_state_str(session->state).c_str());
      continue;
    }

    // If we are here, we have a valid packet for a connected session
    dpath_dprintf("eRPC Rpc %u: Received packet %s.\n", rpc_id,
                  pkthdr->to_string().c_str());

    // All Expected packets are credit returns, and vice versa
    if (pkthdr->is_unexp == 0) {
      assert(session->credits < Session::kSessionCredits);
      session->credits++;

      // Nothing more to do for explicit credit returns - process other packets
      if (pkthdr->is_exp_cr()) {
        continue;
      }
    }

    assert(pkthdr->is_req() || pkthdr->is_resp());

    if (small_rpc_likely(pkthdr->msg_size <= TTr::kMaxDataPerPkt)) {
      // Optimize for when the received packet is a single-packet message
      process_comps_small_msg_one_st(session, pkt);
    } else {
      process_comps_large_msg_one_st(session, pkt);
    }
  }

  // Technically, these RECVs can be posted immediately after rx_burst(), or
  // even in the rx_burst() code.
  transport->post_recvs(num_pkts);
}

template <class TTr>
void Rpc<TTr>::process_comps_small_msg_one_st(Session *session,
                                              const uint8_t *pkt) {
  assert(in_creator());
  assert(session != nullptr && session->is_connected());
  assert(pkt != nullptr);

  const pkthdr_t *pkthdr = reinterpret_cast<const pkthdr_t *>(pkt);
  assert(pkthdr->check_magic() && pkthdr->pkt_num == 0);
  assert(pkthdr->msg_size > 0 &&  // Credit returns already handled
         pkthdr->msg_size <= TTr::kMaxDataPerPkt);
  assert(pkthdr->is_req() || pkthdr->is_resp());

  // Extract packet header fields
  uint8_t req_type = pkthdr->req_type;
  size_t req_num = pkthdr->req_num;
  size_t msg_size = pkthdr->msg_size;
  bool is_req = pkthdr->is_req();

  const ReqFunc &req_func = req_func_arr[req_type];
  if (unlikely(!req_func.is_registered())) {
    erpc_dprintf(
        "eRPC Rpc %u: Warning: Received packet for unknown request type %u. "
        "Dropping packet.\n",
        rpc_id, req_type);
    return;
  }

  // For small messages, explicit credit return packets are sent if the
  // request handler is not foreground-terminal.
  if (small_rpc_unlikely((is_req && !req_func.is_fg_terminal()) ||
                         (!is_req && pkthdr->fgt_resp == 0))) {
    send_credit_return_now_st(session, pkthdr);
  }

  // Create the RX MsgBuffer in the message's session slot
  size_t sslot_i = req_num % Session::kSessionReqWindow;  // Bit shift
  SSlot &sslot = session->sslot_arr[sslot_i];

  // The RX MsgBuffer stored previously in this slot was buried earlier
  assert(sslot.rx_msgbuf.buf == nullptr);
  assert(sslot.rx_msgbuf.buffer.buf == nullptr);

  if (pkthdr->is_req()) {
    // Handle a single-packet request message
    assert(session->is_server());

    // Bury the previous possibly-dynamic response MsgBuffer (tx_msgbuf)
    bury_sslot_tx_msgbuf(&sslot);

    // Remember request metadata for enqueue_response()
    sslot.srv_save_info.req_func_type = req_func.req_func_type;
    sslot.srv_save_info.req_type = req_type;
    sslot.srv_save_info.req_num = req_num;

    if (small_rpc_likely(!req_func.is_background())) {
      // For foreground (terminal/non-terminal) request handlers, a "fake",
      // MsgBuffer suffices (i.e., it's valid for the duration of req_func).
      sslot.rx_msgbuf = MsgBuffer(pkt, msg_size);
      req_func.req_func(static_cast<ReqHandle *>(&sslot), context);
      bury_sslot_rx_msgbuf_nofree(&sslot);
      return;
    } else {
      // For background request handlers, we need a RX ring--independent copy of
      // the request. The allocated rx_msgbuf is freed by the background thread.
      sslot.rx_msgbuf = alloc_msg_buffer(msg_size);
      assert(sslot.rx_msgbuf.buf != nullptr);
      memcpy(reinterpret_cast<char *>(sslot.rx_msgbuf.get_pkthdr_0()), pkt,
             msg_size + sizeof(pkthdr_t));
      submit_background_st(&sslot, Nexus<TTr>::BgWorkItemType::kReq);
      return;
    }
  } else {
    // Handle a single-packet response message
    assert(session->is_client());
    debug_check_req_msgbuf_on_resp(&sslot, req_num, req_type);

    // Bury request MsgBuffer (tx_msgbuf) without freeing user-owned memory
    bury_sslot_tx_msgbuf_nofree(&sslot);

    if (small_rpc_likely(!sslot.clt_save_info.is_requester_bg)) {
      // Create a "fake" static MsgBuffer for the foreground continuation
      sslot.rx_msgbuf = MsgBuffer(pkt, msg_size);
      sslot.clt_save_info.cont_func(static_cast<RespHandle *>(&sslot), context,
                                    sslot.clt_save_info.tag);

      // The continuation must release the response (rx_msgbuf), and only the
      // event loop (this thread) can re-use it and make it non-NULL.
      assert(sslot.rx_msgbuf.buf == nullptr);
    } else {
      // For background continuations, we need a RX ring--independent copy of
      // the resp. The allocated rx_msgbuf is freed by the background thread.
      sslot.rx_msgbuf = alloc_msg_buffer(msg_size);
      assert(sslot.rx_msgbuf.buf != nullptr);
      memcpy(reinterpret_cast<char *>(sslot.rx_msgbuf.get_pkthdr_0()), pkt,
             msg_size + sizeof(pkthdr_t));
      submit_background_st(&sslot, Nexus<TTr>::BgWorkItemType::kResp);
      return;
    }
    return;
  }
}

// This function is for large messages, so don't use small_rpc_likely()
template <class TTr>
void Rpc<TTr>::process_comps_large_msg_one_st(Session *session,
                                              const uint8_t *pkt) {
  assert(in_creator());
  assert(session != nullptr && session->is_connected());
  assert(pkt != nullptr);

  const pkthdr_t *pkthdr = reinterpret_cast<const pkthdr_t *>(pkt);
  assert(pkthdr->check_magic());
  assert(pkthdr->msg_size > TTr::kMaxDataPerPkt);  // Multi-packet
  assert(pkthdr->is_req() || pkthdr->is_resp());   // Credit returns are small

  // Extract packet header fields
  uint8_t req_type = pkthdr->req_type;
  size_t req_num = pkthdr->req_num;
  size_t msg_size = pkthdr->msg_size;
  size_t pkt_num = pkthdr->pkt_num;
  bool is_req = pkthdr->is_req();

  const ReqFunc &req_func = req_func_arr[req_type];
  if (unlikely(!req_func.is_registered())) {
    fprintf(stderr,
            "eRPC Rpc %u: Warning: Received packet for unknown "
            "request type %u. Dropping packet.\n",
            rpc_id, req_type);
    return;
  }

  // For large messages, explicit credit return packets for are sent for:
  // 1. Foreground-terminal request handlers:
  //   1a. Non-last request packets
  //   1b. Non-first response packets
  // 2. Non-foreground-terminal request handlers: All packets
  bool is_req_handler_fgt = (is_req && req_func.is_fg_terminal()) ||
                            (!is_req && pkthdr->fgt_resp == 1);

  size_t pkts_expected =
      (msg_size + TTr::kMaxDataPerPkt - 1) / TTr::kMaxDataPerPkt;
  bool is_last = (pkt_num == pkts_expected - 1);

  if (is_req_handler_fgt) {
    // Foreground-terminal request handler (1a. and 1b. above)
    if ((pkthdr->is_req() && !is_last) || (pkthdr->is_resp() && pkt_num != 0)) {
      send_credit_return_now_st(session, pkthdr);
    }
  } else {
    // Non-foreground-terminal request handler (2. above)
    send_credit_return_now_st(session, pkthdr);
  }

  // Continue non-credit return processing
  size_t sslot_i = req_num % Session::kSessionReqWindow;  // Bit shift
  SSlot &sslot = session->sslot_arr[sslot_i];
  MsgBuffer &rx_msgbuf = sslot.rx_msgbuf;

  // Basic checks
  if (is_req) {
    assert(session->is_server());
  } else {
    assert(session->is_client());
    debug_check_req_msgbuf_on_resp(&sslot, req_num, req_type);
  }

  if (rx_msgbuf.buf == nullptr) {
    // This is the first time that we have received a packet for this message.
    // (This may not be the zeroth packet of the message due to reordering.)
    //
    // The RX MsgBuffer stored previously in this slot was buried earlier.
    assert(rx_msgbuf.buffer.buf == nullptr);

    if (pkthdr->is_req()) {
      // Bury the previous possibly-dynamic response MsgBuffer (tx_msgbuf)
      bury_sslot_tx_msgbuf(&sslot);
    }

    if (unlikely(pkt_num != 0)) {
      erpc_dprintf(
          "eRPC Rpc %u: Received out-of-order packet on session %u. "
          "Expected packet number = 0, received = %zu.\n",
          rpc_id, session->local_session_num, pkt_num);
    }

    rx_msgbuf = alloc_msg_buffer(msg_size);
    assert(sslot.rx_msgbuf.buf != nullptr);
    rx_msgbuf.pkts_rcvd = 1;

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

    assert(rx_msgbuf.pkts_rcvd >= 1);
    rx_msgbuf.pkts_rcvd++;
  }

  // Copy the received packet's data only. The message's common packet header
  // was copied to rx_msgbuf.pkthdr_0 earlier.
  size_t offset = pkt_num * TTr::kMaxDataPerPkt;  // rx_msgbuf offset
  size_t bytes_to_copy = is_last ? (msg_size - offset) : TTr::kMaxDataPerPkt;
  assert(bytes_to_copy <= TTr::kMaxDataPerPkt);
  memcpy(reinterpret_cast<char *>(&rx_msgbuf.buf[offset]),
         reinterpret_cast<const char *>(pkt + sizeof(pkthdr_t)), bytes_to_copy);

  // Check if we need to invoke the app handler/continuation
  if (rx_msgbuf.pkts_rcvd != pkts_expected) {
    return;
  }

  // If we're here, we received all packets of this message
  if (pkthdr->is_req()) {
    // Remember request metadata for enqueue_response()
    sslot.srv_save_info.req_func_type = req_func.req_func_type;
    sslot.srv_save_info.req_type = req_type;
    sslot.srv_save_info.req_num = req_num;

    // rx_msgbuf here is independent of the RX ring, so we never need a copy
    if (!req_func.is_background()) {
      req_func.req_func(static_cast<ReqHandle *>(&sslot), context);
      bury_sslot_rx_msgbuf(&sslot);
      return;
    } else {
      submit_background_st(&sslot, Nexus<TTr>::BgWorkItemType::kReq);
      return;
    }
  } else {
    // Bury request MsgBuffer (tx_msgbuf) without freeing user-owned memory
    bury_sslot_tx_msgbuf_nofree(&sslot);

    if (small_rpc_likely(!sslot.clt_save_info.is_requester_bg)) {
      sslot.clt_save_info.cont_func(static_cast<RespHandle *>(&sslot), context,
                                    sslot.clt_save_info.tag);

      // The continuation must release the response (rx_msgbuf), and only the
      // event loop (this thread) can un-bury it.
      assert(sslot.rx_msgbuf.buf == nullptr);
    } else {
      submit_background_st(&sslot, Nexus<TTr>::BgWorkItemType::kResp);
      return;
    }
    return;
  }
}

template <class TTr>
void Rpc<TTr>::submit_background_st(
    SSlot *sslot, typename Nexus<TTr>::BgWorkItemType wi_type) {
  assert(in_creator());
  assert(nexus->num_bg_threads > 0);
  assert(sslot != nullptr);

  if (wi_type == Nexus<TTr>::BgWorkItemType::kReq) {
    assert(sslot->session != nullptr && sslot->session->is_server());
  } else {
    assert(sslot->session != nullptr && sslot->session->is_client());
  }

  // Sanity-check RX and TX MsgBuffers
  debug_check_bg_rx_msgbuf(sslot, wi_type);
  assert(sslot->tx_msgbuf == nullptr);

  // Choose a background thread at random
  size_t bg_thread_id = fast_rand.next_u32() % nexus->num_bg_threads;
  auto *req_list = nexus_hook.bg_req_list_arr[bg_thread_id];

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
  assert(req_msgbuf->pkts_queued == req_msgbuf->num_pkts);
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

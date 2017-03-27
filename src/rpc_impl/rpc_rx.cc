#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::process_completions() {
  assert(in_creator()); /* Only creator runs event loop */
  size_t num_pkts = transport->rx_burst();
  if (num_pkts == 0) {
    return;
  }

  for (size_t i = 0; i < num_pkts; i++) {
    const uint8_t *pkt = rx_ring[rx_ring_head];
    rx_ring_head = mod_add_one<Transport::kRecvQueueDepth>(rx_ring_head);

    const pkthdr_t *pkthdr = (pkthdr_t *)pkt;
    assert(pkthdr->check_magic());
    assert(pkthdr->msg_size <= kMaxMsgSize); /* msg_size can be 0 here */

    uint16_t session_num = pkthdr->rem_session_num; /* Local session */
    assert(session_num < session_vec.size());

    Session *session = session_vec[session_num];
    if (unlikely(session == nullptr)) {
      fprintf(stderr,
              "eRPC Rpc %u: Warning: Received packet for buried session %u. "
              "Dropping packet.\n",
              app_tid, session_num);
      continue;
    }

    if (unlikely(!session->is_connected())) {
      fprintf(stderr,
              "eRPC Rpc %u: Warning: Received packet for unconnected "
              "session %u. Session state is %s. Dropping packet.\n",
              app_tid, session_num, session_state_str(session->state).c_str());
      continue;
    }

    /* If we are here, we have a valid packet for a connected session */
    dpath_dprintf("eRPC Rpc %u: Received packet %s.\n", app_tid,
                  pkthdr->to_string().c_str());

    /* All Expected packets are session/window credit returns, and vice versa */
    if (pkthdr->is_unexp == 0) {
      assert(unexp_credits < kRpcUnexpPktWindow);
      assert(session->remote_credits < Session::kSessionCredits);
      unexp_credits++;
      session->remote_credits++;

      /* Nothing more to do for credit returns - process other packets */
      if (pkthdr->is_credit_return()) {
        continue;
      }
    }

    assert(pkthdr->is_req() || pkthdr->is_resp());

    if (small_rpc_likely(pkthdr->msg_size <= TTr::kMaxDataPerPkt)) {
      /* Optimize for when the received packet is a single-packet message */
      process_completions_small_msg_one(session, pkt);
    } else {
      process_completions_large_msg_one(session, pkt);
    }
  }

  /*
   * Technically, these RECVs can be posted immediately after rx_burst(), or
   * even in the rx_burst() code.
   */
  transport->post_recvs(num_pkts);
}

template <class TTr>
void Rpc<TTr>::process_completions_small_msg_one(Session *session,
                                                 const uint8_t *pkt) {
  assert(in_creator()); /* Only creator runs event loop */
  assert(session != nullptr && session->is_connected());
  assert(pkt != nullptr && ((pkthdr_t *)pkt)->check_magic());

  const pkthdr_t *pkthdr = (pkthdr_t *)pkt; /* A valid packet header */
  assert(pkthdr->pkt_num == 0);
  assert(pkthdr->msg_size > 0 && /* Credit returns already handled */
         pkthdr->msg_size <= TTr::kMaxDataPerPkt);
  assert(pkthdr->is_req() || pkthdr->is_resp());

  /* Extract packet header fields */
  uint8_t req_type = pkthdr->req_type;
  size_t req_num = pkthdr->req_num;
  size_t msg_size = pkthdr->msg_size;
  bool is_req = pkthdr->is_req();

  const ReqFunc &req_func = req_func_arr[req_type];
  if (unlikely(!req_func.is_registered())) {
    erpc_dprintf(
        "eRPC Rpc %u: Warning: Received packet for unknown request type %u. "
        "Dropping packet.\n",
        app_tid, req_type);
    return;
  }

  /*
   * Send a credit return if needed.
   *
   * For small messages, explicit credit return packets are sent if the
   * request handler is not foreground-terminal.
   */
  if (small_rpc_unlikely((is_req && !req_func.is_fg_terminal()) ||
                         (!is_req && pkthdr->fgt_resp == 0))) {
    send_credit_return_now(session, pkthdr);
  }

  /* Create the RX MsgBuffer in the message's session slot */
  size_t sslot_i = req_num % Session::kSessionReqWindow; /* Bit shift */
  SSlot &sslot = session->sslot_arr[sslot_i];

  /*
   * The RX MsgBuffer stored previously in this slot was buried earlier: The
   * server (client) buried it after the request (response) handler returned.
   */
  assert(sslot.rx_msgbuf.buf == nullptr);
  assert(sslot.rx_msgbuf.buffer.buf == nullptr);

  if (pkthdr->is_req()) {
    // Handle a single-packet request message
    assert(session->is_server());

    /* Bury the previous possibly-dynamic response MsgBuffer (tx_msgbuf) */
    bury_sslot_tx_msgbuf(&sslot);

    sslot.req_func_type = req_func.req_func_type; /* Set the req func type */

    if (small_rpc_likely(req_func.is_fg_terminal())) {
      /*
       * Create a "fake" static MsgBuffer for the foreground terminal
       * request handler
       */
      sslot.rx_msgbuf = MsgBuffer(pkt, msg_size);
      req_func.req_func((ReqHandle *)&sslot, &sslot.rx_msgbuf, context);
      return;
    } else {
      /*
       * For foreground non-terminal and background request handlers, make a
       * copy of the request, including pkthdr (needed for enqueue_response).
       */
      sslot.rx_msgbuf = alloc_msg_buffer(msg_size);
      memcpy((char *)sslot.rx_msgbuf.get_pkthdr_0(), pkt,
             msg_size + sizeof(pkthdr_t));

      /* Check the copy */
      assert(sslot.rx_msgbuf.is_req());
      assert(sslot.rx_msgbuf.get_req_type() == req_type);
      assert(sslot.rx_msgbuf.get_req_num() == req_num);

      if (!req_func.is_background()) {
        req_func.req_func((ReqHandle *)&sslot, &sslot.rx_msgbuf, context);
      } else {
        submit_bg(&sslot);
      }

      return;
    }
  } else {
    // Handle a single-packet response message
    assert(session->is_client());

    /* Sanity-check the user's request MsgBuffer. This always has a Buffer. */
    const MsgBuffer *req_msgbuf = sslot.tx_msgbuf;
    _unused(req_msgbuf);
    assert(req_msgbuf != nullptr);
    assert(req_msgbuf->buf != nullptr && req_msgbuf->check_magic());
    assert(req_msgbuf->is_req());
    assert(req_msgbuf->get_req_num() == req_num);
    assert(req_msgbuf->pkts_queued == req_msgbuf->num_pkts);

    /* Use pre_resp_msgbuf as the response MsgBuffer */
    sslot.pre_resp_msgbuf.resize(msg_size, 1);
    memcpy((char *)sslot.pre_resp_msgbuf.get_pkthdr_0(), pkt,
           msg_size + sizeof(pkthdr_t));
    sslot.pre_resp_msgbuf.pkts_rcvd = 1;

    /* Bury request MsgBuffer (tx_msgbuf) without freeing user-owned memory */
    bury_sslot_tx_msgbuf_nofree(&sslot);
    sslot.cont_func((RespHandle *)&sslot, &sslot.pre_resp_msgbuf, context,
                    sslot.tag);
    return;
  }
}

/* This function is for large messages, so don't use small_rpc_likely() */
template <class TTr>
void Rpc<TTr>::process_completions_large_msg_one(Session *session,
                                                 const uint8_t *pkt) {
  assert(in_creator()); /* Only creator runs event loop */
  assert(session != nullptr && session->is_connected());
  assert(pkt != nullptr && ((pkthdr_t *)pkt)->check_magic());

  const pkthdr_t *pkthdr = (pkthdr_t *)pkt;       /* A valid packet header */
  assert(pkthdr->msg_size > TTr::kMaxDataPerPkt); /* Multi-packet */
  assert(pkthdr->is_req() || pkthdr->is_resp());  /* Credit returns are small */

  /* Extract packet header fields */
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
            app_tid, (uint8_t)req_type);
    return;
  }

  /*
   * Send a credit return if needed. For large messages, explicit credit return
   * packets for are sent in the following cases:
   *
   * 1. Background request handler: All packets
   * 2. Foreground request handler:
   *   1a. Non-last request packets
   *   1b. Non-first response packets
   */
  bool is_req_handler_bg = (is_req && req_func.is_background()) ||
                           (!is_req && pkthdr->fgt_resp == 0);

  size_t pkts_expected =
      (msg_size + TTr::kMaxDataPerPkt - 1) / TTr::kMaxDataPerPkt;
  bool is_last = (pkt_num == pkts_expected - 1);

  if (is_req_handler_bg || (pkthdr->is_req() && !is_last) ||
      (pkthdr->is_resp() && pkt_num != 0)) {
    send_credit_return_now(session, pkthdr);
    /* Continue processing */
  }

  size_t sslot_i = req_num % Session::kSessionReqWindow; /* Bit shift */
  SSlot &sslot = session->sslot_arr[sslot_i];
  MsgBuffer &rx_msgbuf = sslot.rx_msgbuf;

  /* Basic checks */
  if (is_req) {
    assert(session->is_server());
  } else {
    assert(session->is_client());

    /* Sanity-check the request MsgBuffer, which may be statically allocated */
    const MsgBuffer *req_msgbuf = sslot.tx_msgbuf;
    _unused(req_msgbuf);
    assert(req_msgbuf != nullptr);
    assert(req_msgbuf->buf != nullptr && req_msgbuf->check_magic());
    assert(req_msgbuf->is_req());
    assert(req_msgbuf->get_req_num() == req_num);
    assert(req_msgbuf->get_req_type() == req_type);
    assert(req_msgbuf->pkts_queued == req_msgbuf->num_pkts);
  }

  if (rx_msgbuf.buf == nullptr) {
    /*
     * This is the first time that we have received a message for this packet.
     * (This may not be the zeroth packet of the message due to reordering.)
     *
     * The RX MsgBuffer stored previously in this slot was buried earlier: The
     * server (client) buried it after the request (response) handler returned.
     */
    assert(rx_msgbuf.buffer.buf == nullptr);

    if (pkthdr->is_req()) {
      /* Bury the previous possibly-dynamic response MsgBuffer (tx_msgbuf) */
      bury_sslot_tx_msgbuf(&sslot);
    }

    if (unlikely(pkt_num != 0)) {
      erpc_dprintf(
          "eRPC Rpc %u: Received out-of-order packet on session %u. "
          "Expected packet number = 0, received = %zu.\n",
          app_tid, session->local_session_num, pkt_num);
    }

    rx_msgbuf = alloc_msg_buffer(msg_size);
    rx_msgbuf.pkts_rcvd = 1;

    /*
     * Store the received packet header into the zeroth rx_msgbuf packet header.
     * It's possible to copy fewer fields, but copying pkthdr_t is cheap.
     */
    *(rx_msgbuf.get_pkthdr_0()) = *pkthdr;
    rx_msgbuf.get_pkthdr_0()->pkt_num = 0;
  } else {
    /*
     * This is not the 1st packet received for this message, so we have a valid,
     * dynamically-allocated RX MsgBuffer.
     */
    assert(rx_msgbuf.buf != nullptr);
    assert(rx_msgbuf.is_dynamic() && rx_msgbuf.check_magic());
    assert(rx_msgbuf.get_req_type() == req_type);
    assert(rx_msgbuf.get_req_num() == req_num);

    assert(rx_msgbuf.pkts_rcvd >= 1);
    rx_msgbuf.pkts_rcvd++;
  }

  /*
   * Copy the received packet's data only. The message's common packet header
   * was copied to rx_msgbuf.pkthdr_0 earlier.
   */
  size_t offset = pkt_num * TTr::kMaxDataPerPkt; /* rx_msgbuf offset */
  size_t bytes_to_copy = is_last ? (msg_size - offset) : TTr::kMaxDataPerPkt;
  assert(bytes_to_copy <= TTr::kMaxDataPerPkt);
  memcpy((char *)&rx_msgbuf.buf[offset], (char *)(pkt + sizeof(pkthdr_t)),
         bytes_to_copy);

  /* Check if we need to invoke the app handler */
  if (rx_msgbuf.pkts_rcvd != pkts_expected) {
    return;
  }

  /* If we're here, we received all packets of this message */
  if (pkthdr->is_req()) {
    sslot.req_func_type = req_func.req_func_type;

    if (!req_func.is_background()) {
      /* Works for both terminal and non-terminal request functions */
      req_func.req_func((ReqHandle *)&sslot, &sslot.rx_msgbuf, context);
      return;
    } else {
      /* We don't depend on any RX ring, so don't create a new MsgBuffer*/
      submit_bg(&sslot);
      return;
    }
  } else {
    /* Bury request MsgBuffer (tx_msgbuf) without freeing user-owned memory */
    bury_sslot_tx_msgbuf_nofree(&sslot);
    sslot.cont_func((RespHandle *)&sslot, &sslot.rx_msgbuf, context, sslot.tag);
    return;
  }
}

template <class TTr>
void Rpc<TTr>::submit_bg(SSlot *sslot) {
  assert(sslot != nullptr);
  assert(sslot->session != nullptr && sslot->session->is_server());

  /* rx_msgbuf (request) must be valid, and tx_msgbuf (response) invalid */
  assert(sslot->rx_msgbuf.buf != nullptr && sslot->rx_msgbuf.check_magic());
  assert(sslot->tx_msgbuf == nullptr);

  assert(nexus->num_bg_threads > 0);

  /* Choose a background thread at random */
  size_t bg_thread_id = fast_rand.next_u32() % nexus->num_bg_threads;
  MtList<BgWorkItem> *req_list = nexus_hook.bg_req_list_arr[bg_thread_id];

  /* Thread-safe */
  req_list->unlocked_push_back(BgWorkItem(app_tid, context, sslot));
}

}  // End ERpc

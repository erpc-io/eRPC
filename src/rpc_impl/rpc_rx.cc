#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::process_completions() {
  size_t num_pkts = transport->rx_burst();
  if (num_pkts == 0) {
    return;
  }

  for (size_t i = 0; i < num_pkts; i++) {
    const uint8_t *pkt = rx_ring[rx_ring_head];
    rx_ring_head = mod_add_one<Transport::kRecvQueueDepth>(rx_ring_head);

    const pkthdr_t *pkthdr = (pkthdr_t *)pkt;
    assert(pkthdr->is_valid());
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

      /* Nothing more to do for credit returns */
      if (pkthdr->is_credit_return()) {
        continue;
      }
    }

    assert(pkthdr->is_req() || pkthdr->is_resp());

    if (small_msg_likely(pkthdr->msg_size <= TTr::kMaxDataPerPkt)) {
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
  assert(session != nullptr && session->is_connected());
  assert(pkt != nullptr && ((pkthdr_t *)pkt)->is_valid());

  const pkthdr_t *pkthdr = (pkthdr_t *)pkt; /* A valid packet header */
  assert(pkthdr->pkt_num == 0);
  assert(pkthdr->msg_size > 0 && /* Credit returns already handled */
         pkthdr->msg_size <= TTr::kMaxDataPerPkt);
  assert(pkthdr->is_req() || pkthdr->is_resp());

  /* Extract packet header fields */
  uint8_t req_type = pkthdr->req_type;
  size_t req_num = pkthdr->req_num;
  size_t msg_size = pkthdr->msg_size;

  const Ops &ops = ops_arr[req_type];
  if (unlikely(!ops.is_valid())) {
    erpc_dprintf(
        "eRPC Rpc %u: Warning: Received packet for unknown request type %u. "
        "Dropping packet.\n",
        app_tid, req_type);
    return;
  }

  /* Create the RX MsgBuffer in the message's session slot */
  size_t sslot_i = req_num % Session::kSessionReqWindow; /* Bit shift */
  Session::sslot_t &sslot = session->sslot_arr[sslot_i];

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
    bury_sslot_tx_msgbuf(sslot);

    if (small_msg_likely(!ops.run_in_background)) {
      /* Create a "fake" static MsgBuffer for the foreground request handler */
      sslot.rx_msgbuf = MsgBuffer(pkt, msg_size);
      sslot.rx_msgbuf.pkts_rcvd = 1;

      ops.req_handler(&sslot.rx_msgbuf, &sslot.app_resp, context);

      /* This uses sslot.rx_msgbuf for pkt header, so delay burying rx_msgbuf */
      enqueue_response(session, sslot);
      assert(sslot.tx_msgbuf != nullptr); /* Installed by enqueue_response */

      /* Bury the non-dynamic request MsgBuffer (rx_msgbuf) created above */
      bury_sslot_rx_msgbuf_nofree(sslot);
      return;
    } else {
      /* Make a copy of the request, including packet header */
      sslot.rx_msgbuf = alloc_msg_buffer(msg_size);
      memcpy((char *)sslot.rx_msgbuf.get_pkthdr_0(), pkt,
             msg_size + sizeof(pkthdr_t));

      /* Check that the copy worked */
      assert(sslot.rx_msgbuf.is_req());
      assert(sslot.rx_msgbuf.get_req_type() == req_type);
      assert(sslot.rx_msgbuf.get_req_num() == req_num);

      submit_bg(session, &sslot);
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

    /* Create a "fake" static MsgBuffer for the foreground response handler */
    sslot.rx_msgbuf = MsgBuffer(pkt, msg_size);
    sslot.rx_msgbuf.pkts_rcvd = 1;

    ops.resp_handler(sslot.tx_msgbuf, &sslot.rx_msgbuf, context);

    /*
     * Bury the request MsgBuffer (tx_msgbuf) without freeing the underlying
     * Buffer, which belongs to the user.
     */
    bury_sslot_tx_msgbuf_nofree(sslot);

    /* Bury the non-dynamic response MsgBuffer (rx_msgbuf) created above */
    bury_sslot_rx_msgbuf_nofree(sslot);

    session->sslot_free_vec.push_back(sslot_i);
    return;
  }
}

template <class TTr>
void Rpc<TTr>::process_completions_large_msg_one(Session *session,
                                                 const uint8_t *pkt) {
  assert(session != nullptr && session->is_connected());
  assert(pkt != nullptr && ((pkthdr_t *)pkt)->is_valid());

  const pkthdr_t *pkthdr = (pkthdr_t *)pkt;       /* A valid packet header */
  assert(pkthdr->msg_size > TTr::kMaxDataPerPkt); /* Multi-packet */
  assert(pkthdr->is_req() || pkthdr->is_resp());  /* Credit returns are small */

  /* Extract packet header fields */
  uint8_t req_type = pkthdr->req_type;
  size_t req_num = pkthdr->req_num;
  size_t msg_size = pkthdr->msg_size;
  size_t pkt_num = pkthdr->pkt_num;

  const Ops &ops = ops_arr[req_type];
  if (unlikely(!ops.is_valid())) {
    fprintf(stderr,
            "eRPC Rpc %u: Warning: Received packet for unknown "
            "request type %u. Dropping packet.\n",
            app_tid, (uint8_t)req_type);
    return;
  }

  size_t sslot_i = req_num % Session::kSessionReqWindow; /* Bit shift */
  Session::sslot_t &sslot = session->sslot_arr[sslot_i];
  MsgBuffer &rx_msgbuf = sslot.rx_msgbuf;

  /* Basic checks */
  if (pkthdr->is_req()) {
    assert(session->is_server());
  } else {
    assert(session->is_client());

    /* Sanity-check the user's request MsgBuffer. This always has a Buffer. */
    const MsgBuffer *req_msgbuf = sslot.tx_msgbuf;
    _unused(req_msgbuf);
    assert(req_msgbuf != nullptr);
    assert(req_msgbuf->buf != nullptr && req_msgbuf->check_magic());
    assert(req_msgbuf->is_req());
    assert(req_msgbuf->get_req_num() == req_num);
    assert(req_msgbuf->get_req_type() == req_type);
    assert(req_msgbuf->pkts_queued == req_msgbuf->num_pkts);
  }

  /*
   * Credit returns are sent for non-last request packets and non-first
   * response packets. The first/last decision is made based on packet number,
   * irrespective of the order in which we receive the packets.
   */
  size_t pkts_expected =
      (msg_size + TTr::kMaxDataPerPkt - 1) / TTr::kMaxDataPerPkt;
  bool is_last = (pkt_num == pkts_expected - 1);

  bool send_cr =
      (pkthdr->is_req() && !is_last) || (pkthdr->is_resp() && pkt_num != 0);
  if (send_cr) {
    assert(tx_batch_i == 0); /* tx_batch_i is 0 outside rpc_tx.cc */
    send_credit_return_now(session);
    /* Continue processing */
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
      bury_sslot_tx_msgbuf(sslot);
    }

    rx_msgbuf = alloc_msg_buffer(msg_size);
    rx_msgbuf.pkts_rcvd = 1;

    /* Store the request type and number in the zeroth packet header */
    rx_msgbuf.get_pkthdr_0()->req_type = req_type;
    rx_msgbuf.get_pkthdr_0()->req_num = req_num;
  } else {
    /* This is a non-first packet, so we have a valid RX MsgBuffer */
    assert(rx_msgbuf.buf != nullptr && rx_msgbuf.check_magic());
    assert(rx_msgbuf.get_req_type() == req_type);
    assert(rx_msgbuf.get_req_num() == req_num);

    assert(rx_msgbuf.pkts_rcvd >= 1);
    rx_msgbuf.pkts_rcvd++;
  }

  /*
   * Copy the received packet's data only. The required packet header fields
   * (req_num and req_type) were copied to rx_msgbuf.pkthdr_0 earlier.
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
    if (small_msg_likely(!ops.run_in_background)) {
      ops.req_handler(&sslot.rx_msgbuf, &sslot.app_resp, context);

      /* This uses sslot.rx_msgbuf for pkt header, so delay burying rx_msgbuf */
      enqueue_response(session, sslot);
      assert(sslot.tx_msgbuf != nullptr); /* Installed by enqueue_response */

      /* Bury the dynamic request MsgBuffer (rx_msgbuf) */
      bury_sslot_rx_msgbuf(sslot);
      return;
    } else {
      /* We don't depend on any RX ring, so don't create a new MsgBuffer*/
      submit_bg(session, &sslot);
      return;
    }
  } else {
    ops.resp_handler(sslot.tx_msgbuf, &sslot.rx_msgbuf, context);

    /*
     * Bury the request MsgBuffer (tx_msgbuf) without freeing the underlying
     * Buffer, which belongs to the user.
     */
    bury_sslot_tx_msgbuf_nofree(sslot);

    /* Bury the dynamic response MsgBuffer (rx_msgbuf) */
    bury_sslot_rx_msgbuf(sslot);

    session->sslot_free_vec.push_back(sslot_i);
    return;
  }
}

template <class TTr>
void Rpc<TTr>::submit_bg(Session *session, Session::sslot_t *sslot) {
  assert(session != nullptr && session->is_server());
  assert(sslot != nullptr);

  /* rx_msgbuf (request) must be valid, and tx_msgbuf (response) invalid */
  assert(sslot->rx_msgbuf.buf != nullptr && sslot->rx_msgbuf.check_magic());
  assert(sslot->tx_msgbuf == nullptr);

  assert(nexus->num_bg_threads > 0);

  /* Choose a background thread at random */
  size_t bg_thread_id = fast_rand.next_u32() % nexus->num_bg_threads;
  MtList<BgWorkItem> *req_list = nexus_hook.bg_req_list_arr[bg_thread_id];

  /* Thread-safe */
  req_list->unlocked_push_back(BgWorkItem(app_tid, context, session, sslot));
}

template <class TTr>
void Rpc<TTr>::handle_bg_responses() {
  MtList<BgWorkItem> &resp_list = nexus_hook.bg_resp_list;
  assert(resp_list.size > 0);

  // Process all responses in the response list
  resp_list.lock();

  for (BgWorkItem bg_work_item : resp_list.list) {
    assert(bg_work_item.app_tid == app_tid);
    Session *session = bg_work_item.session;
    Session::sslot_t *sslot = bg_work_item.sslot;

    /* Sanity-check rx_msgbuf. It must be dynamic. */
    assert(sslot->rx_msgbuf.buf != nullptr && sslot->rx_msgbuf.check_magic());
    assert(sslot->rx_msgbuf.is_dynamic());
    assert(sslot->rx_msgbuf.is_req());

    /* This uses sslot.rx_msgbuf for pkt header, so delay burying rx_msgbuf */
    enqueue_response(session, *sslot);
    assert(sslot->tx_msgbuf != nullptr); /* Installed by enqueue_response */

    /* Bury the dynamic request MsgBuffer (rx_msgbuf) */
    bury_sslot_rx_msgbuf(*sslot);
  }

  resp_list.locked_clear(); /* Empty the response list */
  resp_list.unlock();
}

}  // End ERpc

#include "rpc.h"

namespace ERpc {

template <class Transport_>
void Rpc<Transport_>::process_completions() {
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

    if (small_msg_likely(pkthdr->msg_size <= Transport_::kMaxDataPerPkt)) {
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

template <class Transport_>
void Rpc<Transport_>::process_completions_small_msg_one(Session *session,
                                                        const uint8_t *pkt) {
  assert(session != nullptr && session->is_connected());
  assert(pkt != nullptr && ((pkthdr_t *)pkt)->is_valid());

  const pkthdr_t *pkthdr = (pkthdr_t *)pkt; /* A valid packet header */
  assert(pkthdr->pkt_num == 0);
  assert(pkthdr->msg_size > 0 && /* Credit returns already handled */
         pkthdr->msg_size <= Transport_::kMaxDataPerPkt);
  assert(pkthdr->is_req() || pkthdr->is_resp());

  const Ops &ops = ops_arr[pkthdr->req_type];
  if (unlikely(!ops.is_valid())) {
    fprintf(stderr,
            "eRPC Rpc %u: Warning: Received packet for unknown "
            "request type %lu. Dropping packet.\n",
            app_tid, (uint8_t)pkthdr->req_type);
    return;
  }

  /* Create the RX MsgBuffer in the message's session slot */
  size_t req_num = pkthdr->req_num;
  size_t sslot_i = req_num % Session::kSessionReqWindow; /* Bit shift */
  Session::sslot_t &sslot = session->sslot_arr[sslot_i];

  /*
   * The RX MsgBuffer stored previously in this slot was buried earlier. The
   * server (client) buried it after the request (response) handler returned.
   */
  assert(sslot.rx_msgbuf.buffer.buf == nullptr);
  assert(sslot.rx_msgbuf.buf == nullptr);

  sslot.rx_msgbuf = MsgBuffer(pkt, pkthdr->msg_size);
  sslot.rx_msgbuf.pkts_rcvd = 1;

  if (pkthdr->is_req()) {
    // Handle a single-packet request message
    assert(session->is_server());
    assert(sslot.req_num == kInvalidReqNum || sslot.req_num < req_num);

    /*
     * Free the application response MsgBuffer for the previous request, as it
     * could have been dynamic.
     */
    bury_sslot_dynamic_app_resp_msgbuf(sslot);

    /* Fill in new sslot info */
    sslot.req_type = pkthdr->req_type;
    sslot.req_num = req_num;

    /* Invoke the request handler, and bury the non-dynamic RX MsgBuffer */
    ops.req_handler(&sslot.rx_msgbuf, &sslot.app_resp, context);
    sslot.rx_msgbuf.buf = nullptr;

    send_response(session, sslot); /* Works for both small and large response */
  } else {
    // Handle a single-packet response message
    assert(session->is_client());

    /* Sanity-check sslot */
    assert(sslot.req_type == pkthdr->req_type);
    assert(sslot.req_num == req_num);

    /* Sanity-check the old req MsgBuffer */
    const MsgBuffer *req_msgbuf = sslot.tx_msgbuf;
    _unused(req_msgbuf);
    assert(req_msgbuf != nullptr);
    assert(req_msgbuf->buf != nullptr && req_msgbuf->check_magic());
    assert(req_msgbuf->is_req());
    assert(req_msgbuf->get_req_num() == req_num);
    assert(req_msgbuf->pkts_queued == req_msgbuf->num_pkts);

    /* Invoke the response callback, and bury the non-dynamic RX MsgBuffer */
    ops.resp_handler(sslot.tx_msgbuf, &sslot.rx_msgbuf, context);
    sslot.rx_msgbuf.buf = nullptr;

    session->sslot_free_vec.push_back(sslot_i);
  }
}

template <class Transport_>
void Rpc<Transport_>::process_completions_large_msg_one(Session *session,
                                                        const uint8_t *pkt) {
  assert(session != nullptr && session->is_connected());
  assert(pkt != nullptr && ((pkthdr_t *)pkt)->is_valid());

  const pkthdr_t *pkthdr = (pkthdr_t *)pkt; /* A valid packet header */
  assert(pkthdr->msg_size > Transport_::kMaxDataPerPkt); /* Multi-packet */
  assert(pkthdr->is_req() || pkthdr->is_resp());

  const Ops &ops = ops_arr[pkthdr->req_type];
  if (unlikely(!ops.is_valid())) {
    fprintf(stderr,
            "eRPC Rpc %u: Warning: Received packet for unknown "
            "request type %lu. Dropping packet.\n",
            app_tid, (uint8_t)pkthdr->req_type);
    return;
  }

  size_t req_num = pkthdr->req_num;
  size_t sslot_i = req_num % Session::kSessionReqWindow; /* Bit shift */
  Session::sslot_t &sslot = session->sslot_arr[sslot_i];

  if (sslot.req_num != req_num) {
    /* This is the first packet of this message */
    assert(sslot.req_num < req_num);

    /*
     * The RX MsgBuffer stored previously in this slot was buried earlier. The
     * server (client) buried it after the request (response) handler returned.
     */
    assert(sslot.rx_msgbuf.buffer.buf == nullptr);
    assert(sslot.rx_msgbuf.buf == nullptr);

    sslot.req_type = pkthdr->req_type;
    sslot.req_num = req_num;
    sslot.rx_msgbuf = alloc_msg_buffer(pkthdr->msg_size);
    // XXX: Memcpy
    sslot.rx_msgbuf.pkts_rcvd = 1;
  }

  // XXX: TODO after this
  size_t pkt_num = pkthdr->pkt_num;
  _unused(pkt_num);

  if (pkthdr->is_req()) {
  } else {
  }
}

}  // End ERpc

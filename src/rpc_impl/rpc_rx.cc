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

    if (unlikely(session->state != SessionState::kConnected)) {
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
                                                        uint8_t *pkt) {
  const pkthdr_t *pkthdr = (pkthdr_t *)pkt; /* A valid packet header */
  assert(pkthdr->pkt_num == 0);
  assert(pkthdr->msg_size > 0); /* Credit returns already handled */
  assert(pkthdr->is_req() || pkthdr->is_resp());

  const Ops &ops = ops_arr[pkthdr->req_type];
  if (unlikely(!ops.is_valid())) {
    fprintf(stderr,
            "eRPC Rpc %u: Warning: Received packet for unknown "
            "request type %u. Dropping packet.\n",
            app_tid, (uint8_t)pkthdr->req_type);
    return;
  }

  /* Create the RX MsgBuffer in the message's session slot */
  size_t req_num = pkthdr->req_num;
  size_t sslot_i = req_num % Session::kSessionReqWindow; /* Bit shift */
  Session::sslot_t &sslot = session->sslot_arr[sslot_i];
  sslot.rx_msgbuf = MsgBuffer(pkt, pkthdr->msg_size);

  if (pkthdr->is_req()) {
    // Handle single-packet request message
    assert(session->is_server());

    /* Sanity-check the session slot. It may or may not be valid */
    assert(sslot.req_num <= req_num);

    sslot.in_free_vec = false;
    sslot.req_type = pkthdr->req_type;
    sslot.req_num = pkthdr->req_num;

    /* Invoke the request handler */
    ops.req_handler(&sslot.rx_msgbuf, &sslot.app_resp, context);
    send_response(session, sslot);
  } else {
    // Handle a single-packet response message
    assert(session->is_client());

    /* Sanity-check the session slot */
    assert(sslot.is_valid());
    assert(sslot.req_num == req_num);
    assert(sslot.req_type == pkthdr->req_type);

    /* Sanity-check the old req MsgBuffer */
    const MsgBuffer *req_msgbuf = sslot.tx_msgbuf;
    _unused(req_msgbuf);
    assert(req_msgbuf != nullptr && req_msgbuf->is_valid());
    assert(req_msgbuf->is_req());
    assert(req_msgbuf->get_req_num() == req_num);
    assert(req_msgbuf->pkts_queued == req_msgbuf->num_pkts);

    /* Invoke the response callback */
    ops.resp_handler(sslot.tx_msgbuf, &sslot.rx_msgbuf, context);

    /* Free the slot, indicating that everything in the slot is garbage */
    sslot.in_free_vec = true;
    session->sslot_free_vec.push_back(sslot_i);
  }
}

template <class Transport_>
void Rpc<Transport_>::process_completions_large_msg_one(Session *session,
                                                        uint8_t *pkt) {
  _unused(session);
  _unused(pkt);
  assert(false);
}

}  // End ERpc

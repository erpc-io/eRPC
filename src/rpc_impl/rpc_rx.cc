#include "rpc.h"

namespace ERpc {

template <class Transport_>
void Rpc<Transport_>::process_completions() {
  size_t num_pkts = transport->rx_burst();
  if (num_pkts == 0) {
    return;
  }

  for (size_t i = 0; i < num_pkts; i++) {
    uint8_t *pkt = rx_ring[rx_ring_head];
    rx_ring_head = mod_add_one<Transport::kRecvQueueDepth>(rx_ring_head);

    pkthdr_t *pkthdr = (pkthdr_t *)pkt;
    assert(pkthdr->magic == kPktHdrMagic);
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
      assert(pkthdr->pkt_num == 0);
      assert(pkthdr->msg_size > 0); /* Credit returns already handled */

      Ops &ops = ops_arr[pkthdr->req_type];
      if (unlikely(!ops.is_valid())) {
        fprintf(stderr,
                "eRPC Rpc %u: Warning: Received packet for unknown "
                "request type %u. Dropping packet.\n",
                app_tid, (uint8_t)pkthdr->req_type);
        continue;
      }

      /* Create the RX MsgBuffer in the message's session slot */
      size_t req_num = pkthdr->req_num;
      size_t sslot_i = req_num % Session::kSessionReqWindow; /* Bit shift */
      Session::sslot_t &sslot = session->sslot_arr[sslot_i];

      sslot.rx_msgbuf = MsgBuffer(pkt, pkthdr->msg_size);

      if (pkthdr->is_req()) {
        // Handle requests
        assert(session->is_server());
        /* The sslot may or may not be in sslot_free_vec */

        /*
         * Invoke the request handler. needs_resp might be useful if the
         * request handler is asynchronous.
         */
        ops.req_handler(&sslot.rx_msgbuf, &sslot.app_resp, context);

        app_resp_t &app_resp = sslot.app_resp;
        size_t resp_size = app_resp.resp_size;
        assert(resp_size > 0);

        if (small_msg_likely(app_resp.prealloc_used)) {
          assert(resp_size <= Transport_::kMaxDataPerPkt);

          app_resp.pre_resp_msgbuf.resize(resp_size, 1);

          /* Fill in packet 0's header */
          pkthdr_t *pkthdr_0 = app_resp.pre_resp_msgbuf.get_pkthdr_0();
          pkthdr_0->req_type = pkthdr->req_type;
          pkthdr_0->msg_size = resp_size;
          pkthdr_0->rem_session_num = session->client.session_num;
          pkthdr_0->pkt_type = kPktTypeResp;
          pkthdr_0->is_unexp = 0; /* First response packet is unexpected */
          pkthdr_0->pkt_num = 0;
          pkthdr_0->req_num = req_num;

          /*
           * Fill in the session message slot. Record that we have a valid
           * request \p req_num and the response.
           */
          sslot.in_free_vec = false;
          sslot.req_num = req_num;
          assert(sslot.rx_msgbuf.buf != nullptr);      /* Valid request */
          sslot.tx_msgbuf = &app_resp.pre_resp_msgbuf; /* Valid response */

          /* Reset queueing progress */
          assert(sslot.tx_msgbuf->num_pkts == 1);
          sslot.tx_msgbuf->pkts_queued = 0;

          upsert_datapath_tx_work_queue(session);
        } else {
          /* A large response to a small request */
          assert(false);
        }
      } else {
        // Handle responses
        assert(pkthdr->is_resp()); /* Cannot be credit return */
        assert(session->is_client());

        assert(!sslot.in_free_vec);

        /* Sanity-check the req MsgBuffer */
        MsgBuffer *req_msgbuf = sslot.tx_msgbuf;
        _unused(req_msgbuf);
        assert(req_msgbuf != nullptr);
        assert(req_msgbuf->buf != nullptr);
        assert(req_msgbuf->check_pkthdr_0());
        assert(req_msgbuf->get_pkthdr_0()->is_req());
        assert(req_msgbuf->get_pkthdr_0()->req_num == req_num);
        assert(req_msgbuf->pkts_queued == req_msgbuf->num_pkts);

        /* Invoke the response callback */
        ops.resp_handler(sslot.tx_msgbuf, &sslot.rx_msgbuf, context);

        /* Free the slot, indicating that everything in the slot is garbage */
        sslot.in_free_vec = true;
        session->sslot_free_vec.push_back(sslot_i);
      }
    } else {
      /* Handle large packets */
      assert(false);
    }
  } /* End loop over received packets */

  /*
   * Technically, these RECVs can be posted immediately after rx_burst(), or
   * even in the rx_burst() code.
   */
  transport->post_recvs(num_pkts);
}

}  // End ERpc

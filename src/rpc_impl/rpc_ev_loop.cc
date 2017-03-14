#include "rpc.h"

namespace ERpc {

template <class Transport_>
void Rpc<Transport_>::run_event_loop_one() {
  /* Handle session management events, if any */
  if (unlikely(sm_hook.session_mgmt_ev_counter > 0)) {
    handle_session_management(); /* Callee grabs the hook lock */
  }

  /* Check if we need to retransmit any session management requests */
  if (unlikely(mgmt_retry_queue.size() > 0)) {
    mgmt_retry();
  }

  process_completions();            /* RX */
  process_datapath_tx_work_queue(); /* TX */
}

template <class Transport_>
void Rpc<Transport_>::process_datapath_tx_work_queue() {
  size_t batch_i = 0; /* Current batch index (<= kPostlist)*/

  /*
   * If we're unable to complete TX for *any* slot of a session, the session is
   * re-added into the TX work queue at this index.
   */
  size_t write_index = 0;

  for (size_t i = 0; i < datapath_tx_work_queue.size(); i++) {
    Session *session = datapath_tx_work_queue[i];

    for (size_t sslot_i = 0; sslot_i < Session::kSessionReqWindow; sslot_i++) {
      Session::sslot_t &sslot = session->sslot_arr[sslot_i];

      /* Process only slots that are busy and need TX */
      if (sslot.in_free_vec || !sslot.needs_tx_queueing) {
        continue;
      }

      MsgBuffer *tx_msgbuf = sslot.tx_msgbuf;
      assert(tx_msgbuf != nullptr);
      assert(tx_msgbuf->buf != nullptr);
      assert(tx_msgbuf->check_pkthdr_0());
      assert(tx_msgbuf->pkts_queued < tx_msgbuf->num_pkts);

      /* If we are here, this message needs packet TX. */
      pkthdr_t *pkthdr_0 = tx_msgbuf->get_pkthdr_0(); /* Debug-only */
      _unused(pkthdr_0);

      if (session->is_client()) {
        assert(pkthdr_0->pkt_type == kPktTypeReq ||
               pkthdr_0->pkt_type == kPktTypeCreditReturn);
      } else {
        assert(pkthdr_0->pkt_type == kPktTypeResp ||
               pkthdr_0->pkt_type == kPktTypeCreditReturn);
      }

      if (small_msg_likely(tx_msgbuf->num_pkts == 1)) {
        /* Optimize for small/credit-return messages that fit in one packet */
        assert(tx_msgbuf->pkts_queued == 0);
        assert(tx_msgbuf->data_size <= Transport_::kMaxDataPerPkt);

        bool is_unexp = (tx_msgbuf->get_pkthdr_0()->is_unexp == 1);
        if (is_unexp) {
          /* If the message is single-pkt and Unexpected, it must be a req */
          assert(tx_msgbuf->get_pkthdr_0()->req_type == kPktTypeReq);
        }

        /* If session credits are on, save & bail if we're out of credits */
        if (kHandleSessionCredits && session->remote_credits == 0 && is_unexp) {
          assert(write_index < datapath_tx_work_queue.size());
          datapath_tx_work_queue[write_index++] = session;

          dpath_dprintf(
              "eRPC Rpc %u: Session %u out of credits. Re-queueing.\n", app_tid,
              session->local_session_num);
          continue; /* Try the next slot - it may be an Expected message */
        }

        /* If Unexpected window is enabled, save & bail if we're out of slots */
        if (kHandleUnexpWindow && unexp_credits == 0 && is_unexp) {
          assert(write_index < datapath_tx_work_queue.size());
          datapath_tx_work_queue[write_index++] = session;

          dpath_dprintf(
              "eRPC Rpc %u: Rpc out of window slots. Re-queueing session %u.",
              app_tid, session->local_session_num);
        }

        if (kHandleUnexpWindow && is_unexp) {
          unexp_credits--;
        }

        if (kHandleSessionCredits && is_unexp) {
          session->remote_credits--;
        }

        assert(batch_i < Transport_::kPostlist);
        tx_burst_item_t &item = tx_burst_arr[batch_i];
        item.routing_info = session->remote_routing_info;
        item.msg_buffer = tx_msgbuf;
        item.offset = 0;
        item.data_bytes = tx_msgbuf->data_size;
        batch_i++;

        /*
         * If we're here, we're going to enqueue this message for tx_burst.
         * This function will execute to completion unless the machine crashes,
         * so it's safe to mark/unmark queueing progress variables now.
         */
        sslot.needs_tx_queueing = false;
        tx_msgbuf->pkts_queued = 1;

        dpath_dprintf(
            "eRPC Rpc %u: Sending single-packet message %s. "
            "Session = %u, slot = %zu.\n",
            app_tid, pkthdr_0->to_string().c_str(), session->local_session_num,
            sslot_i);

        if (batch_i == Transport_::kPostlist) {
          /* This will increment tx_msgbuf's pkts_sent and data_sent */
          transport->tx_burst(tx_burst_arr, batch_i);
          batch_i = 0;
        }

        continue; /* We're done with this message, try the next one */
      }           /* End handling single-packet messages */

      /* If we're here, msg_buffer is a multi-packet message */
      process_datapath_tx_work_queue_multi_pkt_one(session, tx_msgbuf, sslot_i,
                                                   batch_i, write_index);

    } /* End loop over messages of a session */
  }   /* End loop over datapath work queue sessions */

  if (batch_i > 0) {
    transport->tx_burst(tx_burst_arr, batch_i);
    batch_i = 0;
  }

  /* Number of sessions left in the datapath work queue = write_index */
  datapath_tx_work_queue.resize(write_index);
}

template <class Transport_>
void Rpc<Transport_>::process_datapath_tx_work_queue_multi_pkt_one(
    Session *session, MsgBuffer *tx_msgbuf, size_t sslot_i, size_t &batch_i,
    size_t &write_index) {
  /*
   * Preconditions from process_datapath_tx_work_queue(). Session credits and
   * Unexpected window must be enabled if large packts are used.
   */
  assert(session != nullptr);
  assert(tx_msgbuf->num_pkts > 1); /* Must be a multi-packet message */
  assert(tx_msgbuf->pkts_queued < tx_msgbuf->num_pkts);
  assert(kHandleSessionCredits && kHandleUnexpWindow);

  /* A multi-packet message cannot be a credit return */
  uint64_t pkt_type = tx_msgbuf->get_pkthdr_0()->pkt_type;
  assert(pkt_type == kPktTypeReq || pkt_type == kPktTypeResp);

  size_t pkts_pending = tx_msgbuf->num_pkts - tx_msgbuf->pkts_queued; /* >= 1 */
  size_t min_of_credits = std::min(session->remote_credits, unexp_credits);

  size_t now_sending;

  if (pkt_type == kPktTypeReq || tx_msgbuf->pkts_queued >= 1) {
    /* All request packets, and non-first response packets are Unexpected */
    now_sending = std::min(pkts_pending, min_of_credits);
    unexp_credits -= now_sending;
    session->remote_credits -= now_sending;
  } else {
    /*
     * This is a response message for which no packet has been sent. We're going
     * to send at least the first (Expected) packet, as it does not require
     * credits.
     */
    now_sending = 1 + std::min(pkts_pending - 1, min_of_credits);

    /* Response packets except the first use Unexpected credits */
    unexp_credits -= (now_sending - 1);
    session->remote_credits -= (now_sending - 1);
  }

  if (now_sending != pkts_pending) {
    /* If we cannot send all packets, save session for later */
    assert(write_index < datapath_tx_work_queue.size());
    datapath_tx_work_queue[write_index++] = session;
  }

  if (now_sending == 0) {
    dpath_dprintf(
        "eRPC Rpc %u: Cannot send any of %zu remaining packets for "
        "multi-packet %s. Session = %u, slot %zu. "
        "Session credits available = %s, Rpc window credits available = %s.\n",
        app_tid, pkts_pending, pkt_type_str(pkt_type).c_str(),
        session->local_session_num, sslot_i,
        session->remote_credits == 0 ? "NO" : "YES",
        unexp_credits == 0 ? "NO" : "YES");
    return;
  }

  dpath_dprintf(
      "eRPC Rpc %u: Sending %zu of %zu remaining packets for "
      "multi-packet %s (slot %zu in session %u).\n",
      app_tid, now_sending, pkts_pending, pkt_type_str(pkt_type).c_str(),
      sslot_i, session->client.session_num);

  for (size_t i = 0; i < now_sending; i++) {
    tx_burst_item_t &item = tx_burst_arr[batch_i];
    item.routing_info = session->remote_routing_info;
    item.msg_buffer = tx_msgbuf;
    item.offset = tx_msgbuf->pkts_queued * Transport_::kMaxDataPerPkt;
    item.data_bytes =
        (tx_msgbuf->data_size - item.offset) >= Transport_::kMaxDataPerPkt
            ? Transport_::kMaxDataPerPkt
            : (tx_msgbuf->data_size - item.offset);

    /*
     * If we're here, we're going to enqueue part or all or this message for
     * tx_burst. This function will execute to completion unless the machine
     * crashes, so it's safe to mark/unmark queueing progress variables now.
     */
    session->sslot_arr[sslot_i].needs_tx_queueing =
        (now_sending == pkts_pending);
    tx_msgbuf->pkts_queued++;

    batch_i++;

    if (batch_i == Transport_::kPostlist) {
      /* This will increment msg_buffer's pkts_sent and data_sent */
      transport->tx_burst(tx_burst_arr, batch_i);
      batch_i = 0;
    }
  }
}

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
              "eRPC Rpc: Warning: Received packet for buried session %u. "
              "Dropping packet.\n",
              session_num);
      continue;
    }

    if (unlikely(session->state != SessionState::kConnected)) {
      fprintf(stderr,
              "eRPC Rpc: Warning: Received packet for unconnected session %u. "
              "Session state is %s. Dropping packet.\n",
              session_num, session_state_str(session->state).c_str());
      continue;
    }

    /* If we are here, we have a valid packet for a connected session */
    dpath_dprintf("eRPC Rpc: Received packet %s.\n",
                  pkthdr->to_string().c_str());

    /*
     * Handle session & Unexpected window credits early for simplicity.
     * All Expected packets are session/window credit returns, and vice versa.
     */
    if (kHandleUnexpWindow && pkthdr->is_unexp == 0) {
      assert(unexp_credits < kRpcUnexpPktWindow);
      unexp_credits++;
    }

    if (kHandleSessionCredits && pkthdr->is_unexp == 0) {
      assert(session->remote_credits < Session::kSessionCredits);
      session->remote_credits++;
    }

    /* We're done handling credit return packets */
    if ((kHandleSessionCredits || kHandleUnexpWindow) &&
        pkthdr->pkt_type == kPktTypeCreditReturn) {
      continue;
    }

    if (small_msg_likely(pkthdr->msg_size <= Transport_::kMaxDataPerPkt)) {
      /* Optimize for when the received packet is a single-packet message */
      assert(pkthdr->pkt_num == 0);
      assert(pkthdr->msg_size > 0); /* Credit returns already handled */

      Ops &ops = ops_arr[pkthdr->req_type];
      if (unlikely(!ops.is_valid())) {
        fprintf(stderr,
                "eRPC Rpc: Warning: Received packet for unknown "
                "request type %u. Dropping packet.\n",
                (uint8_t)pkthdr->req_type);
        continue;
      }

      /* Create the RX MsgBuffer in the message's session slot */
      size_t req_num = pkthdr->req_num;
      size_t sslot_i = req_num % Session::kSessionReqWindow; /* Bit shift */
      Session::sslot_t &sslot = session->sslot_arr[sslot_i];
      assert(!sslot.needs_tx_queueing);

      sslot.rx_msgbuf = MsgBuffer(pkt, pkthdr->msg_size);

      if (pkthdr->pkt_type == kPktTypeReq) {
        assert(session->is_server());

        assert(!sslot.needs_resp); /* Server sslots never need resps */
        /* The sslot may or may not be in sslot_free_vec */

        /* Invoke the request handler */
        ops.req_handler(&sslot.rx_msgbuf, &sslot.app_resp, context);

        app_resp_t &app_resp = sslot.app_resp;
        size_t resp_size = app_resp.resp_size;
        assert(resp_size > 0);

        if (small_msg_likely(app_resp.prealloc_used)) {
          assert(resp_size <= Transport_::kMaxDataPerPkt);

          MsgBuffer &resp_msgbuf = app_resp.pre_resp_msgbuf;
          resp_msgbuf.resize(resp_size, 1);

          /* Fill in packet 0's header */
          /* XXX: Optimize using preconstructed headers. */
          pkthdr_t *pkthdr_0 = resp_msgbuf.get_pkthdr_0();
          pkthdr_0->req_type = pkthdr->req_type;
          pkthdr_0->msg_size = resp_size;
          pkthdr_0->rem_session_num = session->client.session_num;
          pkthdr_0->pkt_type = kPktTypeResp;
          pkthdr_0->is_unexp = 0; /* First response packet is unexpected */
          pkthdr_0->pkt_num = 0;
          pkthdr_0->req_num = req_num;

          sslot.in_free_vec = false;
          sslot.needs_tx_queueing = true;
          sslot.needs_resp = false;
          sslot.req_num = req_num;
          sslot.tx_msgbuf = &app_resp.pre_resp_msgbuf;

          upsert_datapath_tx_work_queue(session);
        } else {
          /* A large response to a small request */
          assert(false);
        }
      } else {
        assert(pkthdr->pkt_type == kPktTypeResp);
        assert(session->is_client());

        assert(!sslot.in_free_vec);
        assert(sslot.needs_resp);

        /* Sanity-check the req MsgBuffer */
        assert(sslot.tx_msgbuf != nullptr);
        assert(sslot.tx_msgbuf->check_pkthdr_0());
        assert(sslot.tx_msgbuf->get_pkthdr_0()->pkt_type == kPktTypeReq);
        assert(sslot.tx_msgbuf->get_pkthdr_0()->req_num == req_num);

        /* Invoke the response callback */
        ops.resp_handler(sslot.tx_msgbuf, &sslot.rx_msgbuf, context);

        /* Free the slot */
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

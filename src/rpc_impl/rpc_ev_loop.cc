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
  size_t batch_i = 0;     /* Current batch index (<= kPostlist)*/
  size_t write_index = 0; /* Sessions that need more work are re-added here */

  for (size_t i = 0; i < datapath_tx_work_queue.size(); i++) {
    Session *session = datapath_tx_work_queue[i];

    /* XXX: Should we loop over only the in_use slots? */
    for (size_t sslot_i = 0; sslot_i < Session::kSessionReqWindow; sslot_i++) {
      Session::sslot_t *sslot = &session->sslot_arr[sslot_i];
      MsgBuffer *tx_msgbuf = &sslot->tx_msgbuf;

      /* Check if this slot needs TX */
      if (!sslot->in_use || tx_msgbuf->pkts_sent == tx_msgbuf->num_pkts) {
        continue;
      }

      /* If we are here, this message needs packet TX. */
      assert(tx_msgbuf->buf != nullptr);
      assert(tx_msgbuf->check_pkthdr_0());
      uint64_t pkt_type = tx_msgbuf->get_pkthdr_0()->pkt_type; /* Debug-only */
      _unused(pkt_type);
      if (session->role == Session::Role::kClient) {
        assert(pkt_type == kPktTypeReq || pkt_type == kPktTypeCreditReturn);
      } else {
        assert(pkt_type == kPktTypeResp || pkt_type == kPktTypeCreditReturn);
      }

      /* If session credits are enabled, save & bail if we're out of credits. */
      if (kHandleSessionCredits && session->remote_credits == 0) {
        assert(write_index < datapath_tx_work_queue.size());
        datapath_tx_work_queue[write_index++] = session;

        dpath_dprintf("eRPC Rpc %u: Session %u out of credits. Re-queueing.\n",
                      app_tid, session->client.session_num);
        break; /* Try the next session */
      }

      if (small_msg_likely(tx_msgbuf->num_pkts == 1)) {
        /* Optimize for small/credit-return messages that fit in one packet */
        assert(tx_msgbuf->data_sent == 0);
        assert(tx_msgbuf->pkts_sent == 0);

        /* If Unexpected window is enabled, save & bail if we're out of slots */
        if (kHandleUnexpWindow && tx_msgbuf->get_pkthdr_0()->is_unexp == 1) {
          assert(pkt_type != kPktTypeResp); /* Single-pkt resps are expected */

          if (unexp_credits == 0) {
            assert(write_index < datapath_tx_work_queue.size());
            datapath_tx_work_queue[write_index++] = session;

            dpath_dprintf(
                "eRPC Rpc %u: Rpc out of Unexpected window slots. Re-queueing "
                "session %u.",
                app_tid, session->client.session_num);

            continue; /* Try the next message - it may be Expected */
          } else {
            /* Consume an Unexpected window slot */
            unexp_credits--;
          }
        }

        assert(batch_i < Transport_::kPostlist);
        tx_routing_info_arr[batch_i] = session->remote_routing_info;
        tx_msg_buffer_arr[batch_i] = tx_msgbuf;
        batch_i++;

        if (kHandleSessionCredits) {
          session->remote_credits--;
        }

        dpath_dprintf(
            "eRPC Rpc %u: Sending single-packet %s (slot %zu in session %u).\n",
            app_tid, pkt_type_str(pkt_type).c_str(), sslot_i,
            session->client.session_num);

        if (batch_i == Transport_::kPostlist) {
          /* This will increment msg_buffer's pkts_sent and data_sent */
          transport->tx_burst(tx_routing_info_arr, tx_msg_buffer_arr, batch_i);
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
    transport->tx_burst(tx_routing_info_arr, tx_msg_buffer_arr, batch_i);
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
  assert(tx_msgbuf->num_pkts > tx_msgbuf->pkts_sent);
  assert(kHandleSessionCredits && session->remote_credits > 1);
  assert(kHandleUnexpWindow);

  /* A multi-packet message cannot be a credit return */
  uint64_t pkt_type = tx_msgbuf->get_pkthdr_0()->pkt_type;
  assert(pkt_type == kPktTypeReq || pkt_type == kPktTypeResp);

  size_t pkts_pending = tx_msgbuf->num_pkts - tx_msgbuf->pkts_sent;

  /*
   * First compute the number of packets we would send if we did not care
   * about the Unexpected window. Due to preconditions, this is >= 1.
   */
  size_t without_unexp_window = std::min(pkts_pending, session->remote_credits);
  assert(without_unexp_window >= 1);

  size_t now_sending;
  if (pkt_type == kPktTypeReq || tx_msgbuf->pkts_sent >= 1) {
    /* All request packets, and non-first response packets are Unexpected */
    now_sending = std::min(without_unexp_window, unexp_credits);
    unexp_credits -= now_sending;
  } else {
    /*
     * This is a response message for which no packet has been sent. We're going
     * to send at least the first (Expected) packet, as we have >= 1 credits.
     */
    now_sending = 1 + std::min(without_unexp_window - 1, unexp_credits);

    /* Response packets except the first use Unexpected credits */
    unexp_credits -= (now_sending - 1);
  }

  session->remote_credits -= now_sending;

  dpath_dprintf(
      "eRPC Rpc %u: Sending %zu of %zu remaining packets for "
      "multi-packet %s (slot %zu in session %u).\n",
      app_tid, now_sending, pkts_pending, pkt_type_str(pkt_type).c_str(),
      sslot_i, session->client.session_num);

  /* If we cannot send all packets, save session for later */
  if (now_sending != pkts_pending) {
    assert(write_index < datapath_tx_work_queue.size());
    datapath_tx_work_queue[write_index++] = session;
  }

  /* Put all packets to send in the tx batch */
  for (size_t i = 0; i < now_sending; i++) {
    tx_routing_info_arr[batch_i] = session->remote_routing_info;
    tx_msg_buffer_arr[batch_i] = tx_msgbuf;
    batch_i++;

    if (batch_i == Transport_::kPostlist) {
      /* This will increment msg_buffer's pkts_sent and data_sent */
      transport->tx_burst(tx_routing_info_arr, tx_msg_buffer_arr, batch_i);
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

    if (small_msg_likely(pkthdr->msg_size <= Transport_::kMaxDataPerPkt)) {
      /* Optimize for small packets */

    } else {
    }
  }

  /*
   * Technically, these RECVs can be posted immediately after rx_burst(), or
   * even in the rx_burst() code.
   */
  transport->post_recvs(num_pkts);
}

}  // End ERpc

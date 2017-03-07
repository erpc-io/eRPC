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

  process_datapath_tx_work_queue();
  process_completions();
}

template <class Transport_>
void Rpc<Transport_>::process_datapath_tx_work_queue() {
  size_t batch_i = 0;     /* Current batch index (<= kPostlist)*/
  size_t write_index = 0; /* Sessions that need more work are re-added here */

  for (size_t i = 0; i < datapath_tx_work_queue.size(); i++) {
    Session *session = datapath_tx_work_queue[i];
    bool is_req = (session->role == Session::Role::kClient);

    /*
     * XXX: Is it better to loop over the in_use slots only. Doing so will
     * require an additional vector in Session to store in_use slots.
     */
    for (size_t sslot_i = 0; sslot_i < Session::kSessionReqWindow; sslot_i++) {
      Session::sslot_t *sslot = &session->sslot_arr[sslot_i];
      MsgBuffer *msg_buffer = is_req ? sslot->req_msgbuf : sslot->resp_msgbuf;

      /* Find a message slot for which we need to send packets */
      if (!sslot->in_use || msg_buffer->pkts_sent == msg_buffer->num_pkts) {
        continue;
      }

      /* Sanity check */
      assert(msg_buffer != nullptr && msg_buffer->buf != nullptr);
      assert(msg_buffer->check_pkthdr_0());
      if (is_req) {
        assert(msg_buffer->get_pkthdr_0()->is_req == 1);
      } else {
        assert(msg_buffer->get_pkthdr_0()->is_req == 0);
      }

      /*
       * If we are here, this message needs packet TX.
       *
       * If session credits are enabled, save & bail if we're out of credits.
       */
      if (kHandleSessionCredits && session->remote_credits == 0) {
        assert(write_index < datapath_tx_work_queue.size());
        datapath_tx_work_queue[write_index++] = session;

        dpath_dprintf("eRPC Rpc %u: Session %u out of credits. Re-queueing.\n",
                      app_tid, session->client.session_num);
        break; /* Try the next session */
      }

      /* If we are here, we have session credits */
      if (small_msg_likely(msg_buffer->num_pkts == 1)) {
        /* Optimize for small messages that fit in one packet. */
        assert(msg_buffer->data_sent == 0);
        assert(msg_buffer->pkts_sent == 0);

        /* If Unexpected window is enabled, save & bail if we're out of slots */
        if (kHandleUnexpWindow && msg_buffer->get_pkthdr_0()->is_unexp == 1) {
          assert(is_req); /* Single-packet responses are always expected */

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
        tx_msg_buffer_arr[batch_i] = msg_buffer;
        batch_i++;

        if (kHandleSessionCredits) {
          session->remote_credits--;
        }

        dpath_dprintf(
            "eRPC Rpc %u: Sending single-packet %s (slot %zu in session %u).\n",
            app_tid, is_req ? "request" : "response", sslot_i,
            session->client.session_num);

        if (batch_i == Transport_::kPostlist) {
          /* This will increment msg_buffer's pkts_sent and data_sent */
          transport->tx_burst(tx_routing_info_arr, tx_msg_buffer_arr, batch_i);
          batch_i = 0;
        }

        continue; /* We're done with this message, try the next one */
      }           /* End handling single-packet messages */

      /* If we're here, msg_buffer is a multi-packet message */
      process_datapath_tx_work_queue_multi_pkt_one(session, msg_buffer, sslot_i,
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
    Session *session, MsgBuffer *msg_buffer, size_t sslot_i, size_t &batch_i,
    size_t &write_index) {
  /* Preconditions from process_datapath_tx_work_queue() */
  assert(session != nullptr);
  assert(msg_buffer != nullptr);
  assert(msg_buffer->num_pkts > 1); /* Must be a multi-packet message */
  assert(msg_buffer->pkts_sent < msg_buffer->num_pkts);
  assert(session->remote_credits > 1);

  /* Session credits and Unexpected window must be enabled for larget pkts */
  assert(kHandleSessionCredits);
  assert(kHandleUnexpWindow);

  bool is_req = (session->role == Session::Role::kClient);
  size_t pkts_pending = msg_buffer->num_pkts - msg_buffer->pkts_sent;

  /*
   * First compute the number of packets we would send if we did not care
   * about the Unexpected window. Due to preconditions, there is at lease one.
   */
  size_t without_unexp_window = std::min(pkts_pending, session->remote_credits);
  assert(without_unexp_window >= 1);

  size_t now_sending;
  if (is_req || msg_buffer->pkts_sent >= 1) {
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
      app_tid, now_sending, pkts_pending, is_req ? "request" : "response",
      sslot_i, session->client.session_num);

  /* If we cannot send all packets, save session for later */
  if (now_sending != pkts_pending) {
    assert(write_index < datapath_tx_work_queue.size());
    datapath_tx_work_queue[write_index++] = session;
  }

  /* Put all packets to send in the tx batch */
  for (size_t i = 0; i < now_sending; i++) {
    tx_routing_info_arr[batch_i] = session->remote_routing_info;
    tx_msg_buffer_arr[batch_i] = msg_buffer;
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
    _unused(pkthdr);
  }

  /*
   * Technically, these RECVs can be posted immediately after rx_burst(), or
   * even in the rx_burst() code.
   */
  transport->post_recvs(num_pkts);

  // Do processing

  // Send responses
}

}  // End ERpc

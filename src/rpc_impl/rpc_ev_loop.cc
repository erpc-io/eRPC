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
    bool is_client = (session->role == Session::Role::kClient);

    for (size_t sslot_i = 0; sslot_i < Session::kSessionReqWindow; sslot_i++) {
      Session::sslot_t *sslot = &session->sslot_arr[sslot_i];
      MsgBuffer *msg_buffer =
          is_client ? sslot->req_msgbuf : sslot->resp_msgbuf;

      /* Find a message slot for which we need to send packets */
      if (!sslot->in_use || msg_buffer->pkts_sent == msg_buffer->num_pkts) {
        continue;
      }

      /* Sanity check */
      assert(msg_buffer->buf != nullptr);
      assert(msg_buffer->check_pkthdr_0());
      if (session->role == Session::Role::kClient) {
        assert(msg_buffer->get_pkthdr_0()->is_req == 1);
      } else {
        assert(msg_buffer->get_pkthdr_0()->is_req == 0);
      }

      /*
       * If we are here, this message needs packet transmission.
       * If we don't have credits, save this session for later & bail.
       */
      if (session->remote_credits == 0) {
        assert(write_index < datapath_tx_work_queue.size());
        datapath_tx_work_queue[write_index++] = session;

        dpath_dprintf("eRPC Rpc: Session %u out of credits. Re-queueing.\n",
                      session->client.session_num);
        break; /* Try the next session */
      }

      /* If we are here, we'll send at least one packet */
      if (small_msg_likely(msg_buffer->num_pkts == 1)) {
        /*
         * Optimize for small messages that fit in one packet. In this case,
         * the session will always be removed from the work queue.
         */
        assert(msg_buffer->data_sent == 0);
        assert(msg_buffer->pkts_sent == 0);

        assert(batch_i < Transport_::kPostlist);
        tx_routing_info_arr[batch_i] = session->remote_routing_info;
        tx_msg_buffer_arr[batch_i] = msg_buffer;
        batch_i++;
        session->remote_credits--;

        dpath_dprintf(
            "eRPC Rpc: Sending single-packet %s (slot %zu in session %u).\n",
            is_client ? "request" : "response", sslot_i,
            session->client.session_num);

        if (batch_i == Transport_::kPostlist) {
          /* This will increment msg_buffer's pkts_sent and data_sent */
          transport->tx_burst(tx_routing_info_arr, tx_msg_buffer_arr, batch_i);
          batch_i = 0;
        }

        continue; /* We're done with this message, try the next one */
      }

      /* Handle multi-packet messages */
      size_t pkts_pending = msg_buffer->num_pkts - msg_buffer->pkts_sent;
      size_t now_sending;

      if (pkts_pending <= session->remote_credits) {
        now_sending = pkts_pending; /* All pkts of this message can be sent */
        dpath_dprintf(
            "eRPC Rpc: Sending all remaining %zu packets for "
            "multi-packet %s (slot %zu in session %u).\n",
            now_sending, is_client ? "request" : "response", sslot_i,
            session->client.session_num);
      } else {
        /* We cannot send all msg packets, so save this session for later */
        now_sending = session->remote_credits;
        dpath_dprintf(
            "eRPC Rpc: Sending %zu of %zu remaining packets for "
            "multi-packet %s  (slot %zu in session %u).\n",
            now_sending, pkts_pending, is_client ? "request" : "response",
            sslot_i, session->client.session_num);

        assert(write_index < datapath_tx_work_queue.size());
        datapath_tx_work_queue[write_index++] = session;
      }

      session->remote_credits -= now_sending;

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
void Rpc<Transport_>::process_completions() {
  size_t num_pkts;
  transport->rx_burst(rx_msg_buffer_arr, &num_pkts);
}

}  // End ERpc

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

  process_datapath_work_queue();
}

template <class Transport_>
void Rpc<Transport_>::process_datapath_work_queue() {
  size_t batch_i = 0;     /* Batch index of the packet */
  size_t write_index = 0; /* Sessions that need more work are re-added here */

  for (size_t i = 0; i < datapath_work_queue.size(); i++) {
    Session *session = datapath_work_queue[i];

    for (size_t msg_i = 0; msg_i < Session::kSessionReqWindow; msg_i++) {
      Session::msg_info_t *msg_info = &session->msg_arr[msg_i];
      MsgBuffer *msg_buffer = msg_info->msg_buffer;
      assert(msg_buffer->buf != nullptr);
      assert(msg_buffer->check_pkthdr_0());

      /* Find a message slot for which we need to send packets */
      if (!msg_info->in_use || msg_buffer->pkts_sent == msg_buffer->num_pkts) {
        continue;
      }

      /*
       * If we are here, we need to send packet for this message.
       * If we don't have credits, save this session for later & bail.
       */
      if (session->remote_credits == 0) {
        assert(write_index < datapath_work_queue.size());
        datapath_work_queue[write_index++] = session;
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
        tx_msg_buffer_arr[batch_i] = msg_info->msg_buffer;
        batch_i++;
        session->remote_credits--;

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
      } else {
        /* We cannot send all msg packets, so save this session for later */
        now_sending = session->remote_credits;

        assert(write_index < datapath_work_queue.size());
        datapath_work_queue[write_index++] = session;
      }

      session->remote_credits -= now_sending;

      /* Put all packets to send in the tx batch */
      for (size_t i = 0; i < now_sending; i++) {
        tx_routing_info_arr[batch_i] = session->remote_routing_info;
        tx_msg_buffer_arr[batch_i] = msg_info->msg_buffer;
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
  datapath_work_queue.resize(write_index);
};

}  // End ERpc

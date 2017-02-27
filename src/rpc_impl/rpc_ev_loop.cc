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

      /* Find a message slot for which we need to send packets */
      if (msg_info->in_use && msg_info->msg_bytes_sent != msg_info->msg_size) {
        /* If we don't have credits, save this session for later & bail */
        if (session->remote_credits == 0) {
          assert(write_index < datapath_work_queue.size());
          datapath_work_queue[write_index++] = session;
          break; /* Try the next session */
        }

        /* Optimize for small messages that fit in one packet */
        if (msg_info->msg_size <=
            Transport_::kMTU - sizeof(Transport::pkthdr_t)) {
          assert(msg_info->msg_bytes_sent == 0);
          assert(msg_info->pkt_buffer->is_valid());
          assert(check_pkthdr(msg_info->pkt_buffer));

          assert(batch_i < Transport_::kPostlist);
          tx_routing_info_arr[batch_i] = session->remote_routing_info;
          tx_pkt_buffer_arr[batch_i] = msg_info->pkt_buffer;
          tx_offset_arr[batch_i] = 0;
          msg_info->msg_bytes_sent = msg_info->msg_size; /* All will be sent */

          batch_i++;
          if (batch_i == Transport_::kPostlist) {
            transport->tx_burst(tx_routing_info_arr, tx_pkt_buffer_arr,
                                tx_offset_arr, batch_i);
            batch_i = 0;
          }

          continue; /* We're done with this message, try the next one */
        }

        /* Handle messages that don't fit in one packet */
        assert(false);
      }
    } /* End loop over messages of a session */
  }   /* End loop over datapath work queue sessions */

  if (batch_i > 0) {
    transport->tx_burst(tx_routing_info_arr, tx_pkt_buffer_arr, tx_offset_arr,
                        batch_i);
    batch_i = 0;
  }
};

}  // End ERpc

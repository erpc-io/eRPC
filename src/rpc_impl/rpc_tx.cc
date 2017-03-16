#include "rpc.h"

namespace ERpc {

template <class Transport_>
void Rpc<Transport_>::process_datapath_tx_work_queue() {
  tx_batch_i = 0;

  /*
   * If we're unable to complete TX for *any* slot of a session, the session is
   * re-added into the TX work queue at this index.
   */
  size_t write_index = 0;

  for (size_t i = 0; i < datapath_tx_work_queue.size(); i++) {
    Session *session = datapath_tx_work_queue[i];
    assert(session->in_datapath_tx_work_queue);

    /* The session cannot be disconnected if it's in TX work queue */
    assert(session->state == SessionState::kConnected);

    /* Does this session need more TX after the loop over slots below? */
    bool session_needs_more_tx = false;

    /*
     * We need to process all slots even if we run out of credits (session or
     * Unexpected window) midway. This is because Expected pkts don't need
     * credits.
     */
    for (size_t sslot_i = 0; sslot_i < Session::kSessionReqWindow; sslot_i++) {
      Session::sslot_t &sslot = session->sslot_arr[sslot_i];

      /* Process only slots that are busy */
      if (sslot.in_free_vec) {
        continue;
      }

      /* Process only slots that need TX */
      MsgBuffer *tx_msgbuf = sslot.tx_msgbuf;
      if (tx_msgbuf == nullptr ||
          tx_msgbuf->pkts_queued == tx_msgbuf->num_pkts) {
        continue;
      }

      assert(tx_msgbuf->buf != nullptr);
      assert(tx_msgbuf->check_pkthdr_0());
      assert(tx_msgbuf->pkts_queued < tx_msgbuf->num_pkts);

      /* If we are here, this message needs packet TX. */
      pkthdr_t *pkthdr_0 = tx_msgbuf->get_pkthdr_0(); /* Debug-only */
      _unused(pkthdr_0);

      if (session->is_client()) {
        assert(pkthdr_0->is_req() || pkthdr_0->is_credit_return());
      } else {
        assert(pkthdr_0->is_resp() || pkthdr_0->is_credit_return());
      }

      if (small_msg_likely(tx_msgbuf->num_pkts == 1)) {
        /* Optimize for small/credit-return messages that fit in one packet */
        process_datapath_tx_work_queue_single_pkt_one(session, tx_msgbuf);
      } else {
        process_datapath_tx_work_queue_multi_pkt_one(session, tx_msgbuf);
      }

      /* If sslot still needs TX, the session needs to stay in the work queue */
      if (tx_msgbuf->pkts_queued != tx_msgbuf->num_pkts) {
        session_needs_more_tx = true;
      }
    } /* End loop over messages of a session */

    if (session_needs_more_tx) {
      assert(session->in_datapath_tx_work_queue);
      assert(write_index < datapath_tx_work_queue.size());
      datapath_tx_work_queue[write_index++] = session;
    } else {
      session->in_datapath_tx_work_queue = false;
    }
  } /* End loop over datapath work queue sessions */

  if (tx_batch_i > 0) {
    transport->tx_burst(tx_burst_arr, tx_batch_i);
    tx_batch_i = 0;
  }

  /* Number of sessions left in the datapath work queue = write_index */
  datapath_tx_work_queue.resize(write_index);
}

template <class Transport_>
void Rpc<Transport_>::process_datapath_tx_work_queue_single_pkt_one(
    Session *session, MsgBuffer *tx_msgbuf) {
  assert(tx_msgbuf->pkts_queued == 0);
  assert(tx_msgbuf->data_size <= Transport_::kMaxDataPerPkt);

  pkthdr_t *pkthdr_0 = tx_msgbuf->get_pkthdr_0();
  bool is_unexp = (pkthdr_0->is_unexp == 1);

  if (is_unexp) {
    /* Well-structured app code should avoid exhausting credits */
    if (likely(session->remote_credits > 0 && unexp_credits > 0)) {
      session->remote_credits--;
      unexp_credits--;
    } else {
      /*
       * We cannot make progress if the packet is Unexpected and we're out of
       * either session or Unexpected window credits. In this case, caller will
       * upsert session to the TX work queue.
       */
      if (session->remote_credits == 0) {
        dpath_stat_inc(&session->dpath_stats.remote_credits_exhaused);
      }

      if (unexp_credits == 0) {
        dpath_stat_inc(&dpath_stats.unexp_credits_exhausted);
      }
      return;
    }
  }

  assert(tx_batch_i < Transport_::kPostlist);
  tx_burst_item_t &item = tx_burst_arr[tx_batch_i];
  item.routing_info = session->remote_routing_info;
  item.msg_buffer = tx_msgbuf;
  item.offset = 0;
  item.data_bytes = tx_msgbuf->data_size;
  tx_batch_i++;

  /* If we're here, we're going to enqueue this message for tx_burst */
  tx_msgbuf->pkts_queued = 1;

  dpath_dprintf("eRPC Rpc %u: Sending single-packet message %s (session %u)\n",
                app_tid, pkthdr_0->to_string().c_str(),
                session->local_session_num);

  if (tx_batch_i == Transport_::kPostlist) {
    /* This will increment tx_msgbuf's pkts_sent and data_sent */
    transport->tx_burst(tx_burst_arr, Transport_::kPostlist);
    tx_batch_i = 0;
  }
}

template <class Transport_>
void Rpc<Transport_>::process_datapath_tx_work_queue_multi_pkt_one(
    Session *session, MsgBuffer *tx_msgbuf) {
  assert(tx_msgbuf->num_pkts > 1); /* Must be a multi-packet message */

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

  if (now_sending == 0) {
    assert(min_of_credits == 0);

    if (session->remote_credits == 0) {
      dpath_stat_inc(&session->dpath_stats.remote_credits_exhaused);
    }

    if (unexp_credits == 0) {
      dpath_stat_inc(&dpath_stats.unexp_credits_exhausted);
    }

    return;
  }

  dpath_dprintf(
      "eRPC Rpc %u: Sending %zu of %zu remaining packets for "
      "multi-packet %s (session %u).\n",
      app_tid, now_sending, pkts_pending, pkt_type_str(pkt_type).c_str(),
      session->client.session_num);

  for (size_t i = 0; i < now_sending; i++) {
    tx_burst_item_t &item = tx_burst_arr[tx_batch_i];
    item.routing_info = session->remote_routing_info;
    item.msg_buffer = tx_msgbuf;
    item.offset = tx_msgbuf->pkts_queued * Transport_::kMaxDataPerPkt;
    item.data_bytes = std::min(tx_msgbuf->data_size - item.offset,
                               Transport_::kMaxDataPerPkt);

    /* If we're here, we will enqueue all/part of tx_msgbuf for tx_burst */
    tx_msgbuf->pkts_queued++;

    tx_batch_i++;

    if (tx_batch_i == Transport_::kPostlist) {
      transport->tx_burst(tx_burst_arr, Transport_::kPostlist);
      tx_batch_i = 0;
    }
  }

  if (tx_batch_i > 0) {
    transport->tx_burst(tx_burst_arr, tx_batch_i);
    tx_batch_i = 0;
  }
}

}  // End ERpc

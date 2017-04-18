#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::process_dpath_txq_st() {
  assert(in_creator());
  size_t write_index = 0;  // Re-add incomplete sslots at this index
  tx_batch_i = 0;

  // Prevent background threads from enqueueing while processing the queue
  lock_cond(&dpath_txq_lock);

  for (size_t i = 0; i < dpath_txq.size(); i++) {
    SSlot *sslot = dpath_txq[i];
    assert(sslot != nullptr);

    Session *session = sslot->session;
    assert(session->is_connected());

    MsgBuffer *tx_msgbuf = sslot->tx_msgbuf;
    assert(tx_msgbuf != nullptr);
    assert(tx_msgbuf->buf != nullptr && tx_msgbuf->check_magic());
    assert(tx_msgbuf->pkts_queued < tx_msgbuf->num_pkts);

    pkthdr_t *pkthdr_0 = tx_msgbuf->get_pkthdr_0();  // Debug-only
    _unused(pkthdr_0);

    if (session->is_client()) {
      assert(pkthdr_0->is_req() || pkthdr_0->is_exp_cr());
    } else {
      assert(pkthdr_0->is_resp() || pkthdr_0->is_exp_cr());
    }

    if (small_rpc_likely(tx_msgbuf->num_pkts == 1)) {
      // Optimize for small/credit-return messages that fit in one packet
      process_dpath_txq_small_msg_one_st(session, tx_msgbuf);
    } else {
      process_dpath_txq_large_msg_one_st(session, tx_msgbuf);
    }

    // Sslots that still need TX stay in the queue
    if (tx_msgbuf->pkts_queued != tx_msgbuf->num_pkts) {
      assert(write_index < dpath_txq.size());
      dpath_txq[write_index++] = sslot;
    }
  }

  dpath_txq.resize(write_index);  // Number of sslots left = write_index
  unlock_cond(&dpath_txq_lock);   // Re-allow bg threads to enqueue sslots

  if (tx_batch_i > 0) {
    transport->tx_burst(tx_burst_arr, tx_batch_i);
    tx_batch_i = 0;
  }
}

template <class TTr>
void Rpc<TTr>::process_dpath_txq_small_msg_one_st(Session *session,
                                                  MsgBuffer *tx_msgbuf) {
  assert(in_creator());
  assert(tx_msgbuf->num_pkts == 1 && tx_msgbuf->pkts_queued == 0);
  assert(tx_msgbuf->data_size <= TTr::kMaxDataPerPkt);

  const pkthdr_t *pkthdr_0 = tx_msgbuf->get_pkthdr_0();
  bool is_unexp = (pkthdr_0->is_unexp == 1);

  if (is_unexp) {
    // Well-structured app code should avoid exhausting credits
    if (likely(session->credits > 0)) {
      session->credits--;
    } else {
      // We cannot make progress if the packet is Unexpected and we're out of
      // either session credits. In this case, caller will re-insert the sslot
      // to the TX queue.
      if (session->credits == 0) {
        dpath_stat_inc(&session->dpath_stats.credits_exhaused);
      }
      return;
    }
  }

  // If we're here, we're going to enqueue this message for tx_burst
  tx_msgbuf->pkts_queued = 1;

  assert(tx_batch_i < TTr::kPostlist);
  Transport::tx_burst_item_t &item = tx_burst_arr[tx_batch_i];
  item.routing_info = session->remote_routing_info;
  item.msg_buffer = tx_msgbuf;
  item.offset = 0;
  item.data_bytes = tx_msgbuf->data_size;
  tx_batch_i++;

  dpath_dprintf("eRPC Rpc %u: Sending single-packet message %s (session %u)\n",
                rpc_id, pkthdr_0->to_string().c_str(),
                session->local_session_num);

  if (tx_batch_i == TTr::kPostlist) {
    // This will increment tx_msgbuf's pkts_sent and data_sent
    transport->tx_burst(tx_burst_arr, TTr::kPostlist);
    tx_batch_i = 0;
  }
}

template <class TTr>
void Rpc<TTr>::process_dpath_txq_large_msg_one_st(Session *session,
                                                  MsgBuffer *tx_msgbuf) {
  assert(in_creator());
  assert(tx_msgbuf->num_pkts > 1 &&
         tx_msgbuf->pkts_queued < tx_msgbuf->num_pkts);

  // A multi-packet message cannot be a credit return
  uint64_t pkt_type = tx_msgbuf->get_pkt_type();
  assert(pkt_type == kPktTypeReq || pkt_type == kPktTypeResp);

  size_t pkts_pending = tx_msgbuf->num_pkts - tx_msgbuf->pkts_queued;  // >= 1

  // The number of packets (Expected or Unexpected) that we will send
  size_t now_sending = 0;

  // It's possible to compute now_sending in O(1), but it's more complicated.
  // This is O(1) per packet sent, and uses header info filled in by
  // enqueue_request() or enqueue_response().
  for (size_t i = tx_msgbuf->pkts_queued; i < tx_msgbuf->num_pkts; i++) {
    pkthdr_t *pkthdr_i =
        i == 0 ? tx_msgbuf->get_pkthdr_0() : tx_msgbuf->get_pkthdr_n(i);

    if (pkthdr_i->is_unexp == 0) {
      // Expected packets don't need credits
      now_sending++;
    } else {
      if (session->credits > 0) {
        now_sending++;
        session->credits--;
        session->credits--;
      } else {
        // We cannot send this packet, so we're done. There are no more
        // expected packets in this message. (Even if there were more Expected
        // packets, we wouldn't send them out of order.)
        break;
      }
    }
  }

  if (now_sending == 0) {
    // This can happen very frequently, so don't print a message here
    assert(session->credits == 0);
    dpath_stat_inc(&session->dpath_stats.credits_exhaused);
    return;
  }

  dpath_dprintf(
      "eRPC Rpc %u: Sending %zu of %zu remaining packets for "
      "multi-packet %s (session %u).\n",
      rpc_id, now_sending, pkts_pending, pkt_type_str(pkt_type).c_str(),
      session->local_session_num);

  for (size_t i = 0; i < now_sending; i++) {
    Transport::tx_burst_item_t &item = tx_burst_arr[tx_batch_i];
    item.routing_info = session->remote_routing_info;
    item.msg_buffer = tx_msgbuf;
    item.offset = tx_msgbuf->pkts_queued * TTr::kMaxDataPerPkt;
    item.data_bytes =
        std::min(tx_msgbuf->data_size - item.offset, TTr::kMaxDataPerPkt);

    // If we're here, we will enqueue all/part of tx_msgbuf for tx_burst
    tx_msgbuf->pkts_queued++;
    tx_batch_i++;

    if (tx_batch_i == TTr::kPostlist) {
      transport->tx_burst(tx_burst_arr, TTr::kPostlist);
      tx_batch_i = 0;
    }
  }

  if (tx_batch_i > 0) {
    transport->tx_burst(tx_burst_arr, tx_batch_i);
    tx_batch_i = 0;
  }
}

}  // End ERpc

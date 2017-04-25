#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::process_req_txq_st() {
  assert(in_creator());
  size_t write_index = 0;  // Re-add incomplete sslots at this index

  // Prevent background threads from enqueueing while processing the queue
  lock_cond(&req_txq_lock);

  for (size_t i = 0; i < req_txq.size(); i++) {
    SSlot *sslot = req_txq[i];
    assert(sslot != nullptr);
    assert(sslot->session->is_client());
    assert(sslot->session->is_connected());

    MsgBuffer *req_msgbuf = sslot->tx_msgbuf;
    assert(req_msgbuf != nullptr);
    assert(req_msgbuf->buf != nullptr && req_msgbuf->check_magic());
    assert(req_msgbuf->pkts_queued < req_msgbuf->num_pkts);
    assert(req_msgbuf->is_req());

    if (small_rpc_likely(req_msgbuf->num_pkts == 1)) {
      // Optimize for small messages that fit in one packet
      process_req_txq_small_one_st(sslot, req_msgbuf);
    } else {
      process_req_txq_large_one_st(sslot, req_msgbuf);
    }

    // Sslots that still need TX stay in the queue
    if (req_msgbuf->pkts_queued != req_msgbuf->num_pkts) {
      assert(write_index < req_txq.size());
      req_txq[write_index++] = sslot;
    }
  }

  req_txq.resize(write_index);  // Number of sslots left = write_index
  unlock_cond(&req_txq_lock);   // Re-allow bg threads to enqueue sslots
}

template <class TTr>
void Rpc<TTr>::process_req_txq_small_one_st(SSlot *sslot,
                                            MsgBuffer *req_msgbuf) {
  assert(in_creator());

  // req_msgbuf is generally valid. Do some small request--specific checks.
  assert(req_msgbuf->num_pkts == 1 && req_msgbuf->pkts_queued == 0);
  assert(req_msgbuf->data_size <= TTr::kMaxDataPerPkt);

  Session *session = sslot->session;
  if (likely(session->credits > 0)) {
    session->credits--;
  } else {
    // We cannot make progress if the packet is Unexpected and we're out of
    // credits. In this case, caller will re-insert the sslot to the TX queue.
    if (session->credits == 0) {
      dpath_stat_inc(&session->dpath_stats.credits_exhaused);
    }
    return;
  }

  enqueue_pkt_tx_burst_st(session->remote_routing_info, req_msgbuf, 0,
                          req_msgbuf->data_size);
}

template <class TTr>
void Rpc<TTr>::process_req_txq_large_one_st(SSlot *sslot,
                                            MsgBuffer *req_msgbuf) {
  assert(in_creator());
  assert(req_msgbuf->num_pkts > 1 &&
         req_msgbuf->pkts_queued < req_msgbuf->num_pkts);

  Session *session = sslot->session;
  size_t pkts_pending = req_msgbuf->num_pkts - req_msgbuf->pkts_queued;  // >= 1
  size_t now_sending = std::min(session->credits, pkts_pending);
  session->credits -= now_sending;

  if (now_sending == 0) {
    // This can happen very frequently, so don't print a message here
    assert(session->credits == 0);
    dpath_stat_inc(&session->dpath_stats.credits_exhaused);
    return;
  }

  for (size_t i = 0; i < now_sending; i++) {
    size_t offset = req_msgbuf->pkts_queued * TTr::kMaxDataPerPkt;
    size_t data_bytes =
        std::min(req_msgbuf->data_size - offset, TTr::kMaxDataPerPkt);

    enqueue_pkt_tx_burst_st(session->remote_routing_info, req_msgbuf, offset,
                            data_bytes);
  }
}

template <class TTr>
void Rpc<TTr>::process_bg_resp_txq_st() {
  assert(in_creator());
  size_t write_index = 0;  // Re-add incomplete sslots at this index

  // Prevent background threads from enqueueing while processing the queue
  bg_resp_txq_lock.lock();

  for (size_t i = 0; i < bg_resp_txq.size(); i++) {
    SSlot *sslot = bg_resp_txq[i];
    assert(sslot != nullptr);

    Session *session = sslot->session;
    assert(session->is_server());
    assert(session->is_connected());

    MsgBuffer *resp_msgbuf = sslot->tx_msgbuf;
    assert(resp_msgbuf != nullptr);
    assert(resp_msgbuf->buf != nullptr && resp_msgbuf->check_magic());
    assert(resp_msgbuf->pkts_queued < resp_msgbuf->num_pkts);
    assert(resp_msgbuf->is_resp());

    // Wait for the background thread to bury this sslot's request MsgBuffer
    if (sslot->rx_msgbuf.buf != nullptr) {
      assert(write_index < bg_resp_txq.size());
      bg_resp_txq[write_index++] = sslot;
      continue;  // Process other packets
    }

    // Send the first response packet
    size_t data_bytes = std::min(resp_msgbuf->data_size, TTr::kMaxDataPerPkt);
    enqueue_pkt_tx_burst_st(session->remote_routing_info, resp_msgbuf, 0,
                            data_bytes);
  }

  bg_resp_txq.resize(write_index);  // Number of sslots left = write_index
  bg_resp_txq_lock.unlock();        // Re-allow bg threads to enqueue sslots
}

}  // End ERpc

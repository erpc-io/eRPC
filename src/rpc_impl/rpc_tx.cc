#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::process_req_txq_st() {
  assert(in_creator());
  size_t write_index = 0;  // Re-add incomplete sslots at this index

  for (size_t i = 0; i < req_txq.size(); i++) {
    SSlot *sslot = req_txq[i];
    assert(sslot != nullptr);
    assert(sslot->session->is_client());
    assert(sslot->session->is_connected());

    MsgBuffer *req_msgbuf = sslot->tx_msgbuf;
    assert(req_msgbuf != nullptr);
    assert(req_msgbuf->buf != nullptr && req_msgbuf->check_magic());
    assert(req_msgbuf->is_req());
    assert(sslot->client_info.req_sent < req_msgbuf->num_pkts);

    if (small_rpc_likely(req_msgbuf->num_pkts == 1)) {
      // Optimize for small messages that fit in one packet
      process_req_txq_small_one_st(sslot, req_msgbuf);
    } else {
      process_req_txq_large_one_st(sslot, req_msgbuf);
    }

    // Session slots that still need TX stay in the queue
    if (sslot->client_info.req_sent != req_msgbuf->num_pkts) {
      assert(write_index < req_txq.size());
      req_txq[write_index++] = sslot;
    }
  }

  req_txq.resize(write_index);  // Number of sslots left = write_index
}

template <class TTr>
void Rpc<TTr>::process_req_txq_small_one_st(SSlot *sslot,
                                            MsgBuffer *req_msgbuf) {
  assert(in_creator());

  Session *session = sslot->session;
  if (session->client_info.credits > 0) {
    session->client_info.credits--;
  } else {
    // Out of credits; caller will re-insert the sslot to the request TX queue.
    dpath_stat_inc(session->dpath_stats.credits_exhaused, 1);
    return;
  }

  enqueue_pkt_tx_burst_st(sslot, 0, req_msgbuf->data_size);
  sslot->client_info.req_sent++;
}

template <class TTr>
void Rpc<TTr>::process_req_txq_large_one_st(SSlot *sslot,
                                            MsgBuffer *req_msgbuf) {
  assert(in_creator());

  Session *session = sslot->session;
  size_t pkts_pending = req_msgbuf->num_pkts - sslot->client_info.req_sent;
  size_t now_sending = std::min(session->client_info.credits, pkts_pending);
  session->client_info.credits -= now_sending;

  if (now_sending == 0) {
    // This can happen very frequently, so don't print a message here
    assert(session->client_info.credits == 0);
    dpath_stat_inc(session->dpath_stats.credits_exhaused, 1);
    return;
  }

  for (size_t i = 0; i < now_sending; i++) {
    size_t offset = sslot->client_info.req_sent * TTr::kMaxDataPerPkt;
    size_t data_bytes =
        std::min(req_msgbuf->data_size - offset, TTr::kMaxDataPerPkt);
    enqueue_pkt_tx_burst_st(sslot, offset, data_bytes);
    sslot->client_info.req_sent++;
  }
}

}  // End ERpc

#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::process_req_txq_st() {
  assert(in_dispatch());
  size_t write_index = 0;  // Re-add incomplete sslots at this index

  for (size_t req_i = 0; req_i < req_txq.size(); req_i++) {
    SSlot *sslot = req_txq[req_i];
    MsgBuffer *req_msgbuf = sslot->tx_msgbuf;

    Session *session = sslot->session;
    assert(session->is_client() && session->is_connected());

    if (session->client_info.credits > 0) {
      assert(req_msgbuf->is_valid_dynamic() && req_msgbuf->is_req());
      assert(sslot->client_info.req_sent < req_msgbuf->num_pkts);

      if (req_msgbuf->num_pkts == 1) {
        // Small request
        session->client_info.credits--;
        enqueue_pkt_tx_burst_st(sslot, 0, req_msgbuf->data_size);
        sslot->client_info.req_sent++;
      } else {
        // Large request
        size_t now_sending =
            std::min(session->client_info.credits,
                     req_msgbuf->num_pkts - sslot->client_info.req_sent);
        assert(now_sending > 0);
        session->client_info.credits -= now_sending;

        for (size_t _x = 0; _x < now_sending; _x++) {
          size_t offset = sslot->client_info.req_sent * TTr::kMaxDataPerPkt;
          size_t data_bytes =
              std::min(req_msgbuf->data_size - offset, TTr::kMaxDataPerPkt);
          enqueue_pkt_tx_burst_st(sslot, offset, data_bytes);
          sslot->client_info.req_sent++;
        }
      }
    } else {
      dpath_stat_inc(session->dpath_stats.credits_exhaused, 1);
    }

    // Session slots that still need TX stay in the queue. This needs to happen
    // whether or not the session had credits to begin with.
    if (sslot->client_info.req_sent != req_msgbuf->num_pkts) {
      req_txq[write_index++] = sslot;
    }
  }

  req_txq.resize(write_index);  // Number of sslots left = write_index
}

}  // End erpc

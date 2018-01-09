#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::process_req_txq_st() {
  assert(in_dispatch());
  size_t write_index = 0;  // Re-add incomplete sslots at this index

  for (size_t req_i = 0; req_i < req_txq.size(); req_i++) {
    SSlot *sslot = req_txq[req_i];
    try_req_sslot_tx_st(sslot);

    // Session slots that still need TX stay in the queue. This needs to happen
    // whether or not the session had credits to begin with.
    if (sslot->client_info.req_sent != sslot->tx_msgbuf->num_pkts) {
      req_txq[write_index++] = sslot;
    }
  }

  req_txq.resize(write_index);  // Number of sslots left = write_index
}

}  // End erpc

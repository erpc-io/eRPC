#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::process_credit_stall_queue_st() {
  assert(in_dispatch());
  size_t write_index = 0;  // Re-add incomplete sslots at this index

  for (size_t req_i = 0; req_i < crd_stall_txq.size(); req_i++) {
    SSlot *sslot = crd_stall_txq[req_i];
    try_req_sslot_tx_st(sslot);

    // Session slots that still need TX stay in the queue. This needs to happen
    // whether or not the session had credits to begin with.
    if (sslot->client_info.req_sent != sslot->tx_msgbuf->num_pkts) {
      crd_stall_txq[write_index++] = sslot;
    }
  }

  crd_stall_txq.resize(write_index);  // Number of sslots left = write_index
}

}  // End erpc

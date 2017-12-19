#include "raw_transport.h"

namespace erpc {

// Packets that are the first packet in their MsgBuffer use one DMA, and may
// be inlined. Packets that are not the first packet use two DMAs, and are never
// inlined for simplicity.
void RawTransport::tx_burst(const tx_burst_item_t*, size_t) {}

// This is a slower polling function than the one used in tx_burst: it prints
// a warning message when the number of polling attempts gets too high. This
// overhead is fine because the send queue is flushed rarely.
void RawTransport::poll_send_cq_for_flush(bool first) {
  struct ibv_wc wc;
  size_t num_tries = 0;
  while (ibv_poll_cq(send_cq, 1, &wc) == 0) {
    num_tries++;
    if (num_tries == 1000000000) {
      fprintf(stderr,
              "eRPC: Warning. tx_flush stuck polling for %s signaled wr.\n",
              first ? "first" : "second");
      num_tries = 0;
    }
  }

  if (unlikely(wc.status != 0)) {
    fprintf(stderr, "eRPC: Fatal error. Bad SEND wc status %d\n", wc.status);
    exit(-1);
  }
}

void RawTransport::tx_flush() {}

size_t RawTransport::rx_burst() { return 0; }

void RawTransport::post_recvs(size_t num_recvs) { _unused(num_recvs); }

}  // End erpc

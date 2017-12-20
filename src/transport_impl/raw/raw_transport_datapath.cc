#include "raw_transport.h"

namespace erpc {

// Packets that are the first packet in their MsgBuffer use one DMA, and may
// be inlined. Packets that are not the first packet use two DMAs, and are never
// inlined for simplicity.
void RawTransport::tx_burst(const tx_burst_item_t*, size_t) {}

void RawTransport::tx_flush() {}

size_t RawTransport::rx_burst() { return 0; }

void RawTransport::post_recvs(size_t num_recvs) { _unused(num_recvs); }

}  // End erpc

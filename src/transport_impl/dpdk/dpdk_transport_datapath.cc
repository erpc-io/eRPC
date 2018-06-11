#include "dpdk_transport.h"
#include "util/huge_alloc.h"

namespace erpc {

void DpdkTransport::tx_burst(const tx_burst_item_t*, size_t) {}

void DpdkTransport::tx_flush() {}

size_t DpdkTransport::rx_burst() { return 0; }

void DpdkTransport::post_recvs(size_t) {}
}  // End erpc

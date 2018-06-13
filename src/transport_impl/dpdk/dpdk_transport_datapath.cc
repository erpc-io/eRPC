#include "dpdk_transport.h"
#include "util/huge_alloc.h"

namespace erpc {

void DpdkTransport::tx_burst(const tx_burst_item_t *, size_t) {}

void DpdkTransport::tx_flush() {
  // Nothing to do because we don't zero-copy for now
  return;
}

size_t DpdkTransport::rx_burst() {
  struct rte_mbuf *rx_pkts[kRxBatchSize];
  size_t nb_rx_new = rte_eth_rx_burst(phy_port, qp_id, rx_pkts, kRxBatchSize);

  for (size_t i = 0; i < nb_rx_new; i++) {
    rx_ring[rx_ring_head] = rte_pktmbuf_mtod(rx_pkts[i], uint8_t *);
    assert(rx_ring[rx_ring_head] ==
           reinterpret_cast<uint8_t *>(rx_pkts[i] + RTE_PKTMBUF_HEADROOM));
    rx_ring_head = (rx_ring_head + 1) % kNumRxRingEntries;
  }

  return nb_rx_new;
}

void DpdkTransport::post_recvs(size_t num_recvs) {
  for (size_t i = 0; i < num_recvs; i++) {
    uint8_t *mtod = rx_ring[rx_ring_tail];
    auto *mbuf = reinterpret_cast<rte_mbuf *>(mtod - RTE_PKTMBUF_HEADROOM);
#if DEBUG
    rte_mbuf_sanity_check(mbuf, true /* is_header */);
#endif
    rte_pktmbuf_free(mbuf);

    rx_ring_tail = (rx_ring_tail + 1) % kNumRxRingEntries;
  }
}

}  // End erpc

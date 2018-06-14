#include "dpdk_transport.h"
#include "util/huge_alloc.h"

namespace erpc {

void DpdkTransport::tx_burst(const tx_burst_item_t *tx_burst_arr,
                             size_t num_pkts) {
  rte_mbuf *tx_mbufs[kPostlist];

  for (size_t i = 0; i < num_pkts; i++) {
    const tx_burst_item_t &item = tx_burst_arr[i];
    const MsgBuffer *msg_buffer = item.msg_buffer;
    assert(msg_buffer->is_valid());  // Can be fake for control packets
    assert(item.pkt_idx == 0);       // Only single-sge packets for now

    size_t pkt_size = msg_buffer->get_pkt_size<kMaxDataPerPkt>(0);
    pkthdr_t *pkthdr = msg_buffer->get_pkthdr_0();

    // We can do an 8-byte aligned memcpy as the 2-byte UDP csum is already 0
    static constexpr size_t hdr_copy_sz = kInetHdrsTotSize - 2;
    static_assert(hdr_copy_sz == 40, "");
    memcpy(&pkthdr->headroom[0], item.routing_info, hdr_copy_sz);

    if (kTesting && item.drop) {
      // XXX: Can this cause performance problems?
      auto *eth_hdr = reinterpret_cast<eth_hdr_t *>(pkthdr->headroom);
      memset(&eth_hdr->dst_mac, 0, sizeof(eth_hdr->dst_mac));
    }

    auto *ipv4_hdr =
        reinterpret_cast<ipv4_hdr_t *>(&pkthdr->headroom[sizeof(eth_hdr_t)]);
    assert(ipv4_hdr->check == 0);
    ipv4_hdr->tot_len = htons(pkt_size - sizeof(eth_hdr_t));

    auto *udp_hdr = reinterpret_cast<udp_hdr_t *>(&ipv4_hdr[1]);
    assert(udp_hdr->check == 0);
    udp_hdr->len = htons(pkt_size - sizeof(eth_hdr_t) - sizeof(ipv4_hdr_t));

    // Do a copy :(
    tx_mbufs[i] = rte_pktmbuf_alloc(mempool);
    assert(tx_mbufs[i] != nullptr);
    memcpy(rte_pktmbuf_mtod(tx_mbufs[i], uint8_t *), pkthdr, pkt_size);

    tx_mbufs[i]->nb_segs = 1;
    tx_mbufs[i]->pkt_len = pkt_size;
    tx_mbufs[i]->data_len = pkt_size;

    LOG_TRACE(
        "eRPC DpdkTransport: Sending packet (idx = %zu, drop = %u). "
        "pkthdr = %s. Frame header = %s.\n",
        i, item.drop, pkthdr->to_string().c_str(),
        frame_header_to_string(&pkthdr->headroom[0]).c_str());
  }

  size_t nb_tx_new = rte_eth_tx_burst(phy_port, qp_id, tx_mbufs, num_pkts);
  if (unlikely(nb_tx_new != num_pkts)) {
    size_t retry_count = 0;
    while (nb_tx_new != num_pkts) {
      nb_tx_new += rte_eth_tx_burst(phy_port, qp_id, &tx_mbufs[nb_tx_new],
                                    num_pkts - nb_tx_new);
      retry_count++;
      if (retry_count == 1000000000) {
        LOG_INFO("Rpc %u stuck in rte_eth_tx_burst", rpc_id);
        retry_count = 0;
      }
    }
  }
}

void DpdkTransport::tx_flush() {
  // Nothing to do because we don't zero-copy for now
  return;
}

size_t DpdkTransport::rx_burst() {
  struct rte_mbuf *rx_pkts[kRxBatchSize];
  size_t nb_rx_new = rte_eth_rx_burst(phy_port, qp_id, rx_pkts, kRxBatchSize);

  if (nb_rx_new == 0) {
    LOG_TRACE("eRPC DpdkTransport: Received zero packets.\n");
    usleep(1000);
  }

  for (size_t i = 0; i < nb_rx_new; i++) {
    rx_ring[rx_ring_head] = rte_pktmbuf_mtod(rx_pkts[i], uint8_t *);
    assert(rx_ring[rx_ring_head] ==
           reinterpret_cast<uint8_t *>(rx_pkts[i]) + sizeof(rte_mbuf) +
               RTE_PKTMBUF_HEADROOM);

    auto *pkthdr = reinterpret_cast<pkthdr_t *>(rx_ring[rx_ring_head]);
    _unused(pkthdr);
    LOG_TRACE(
        "eRPC DpdkTransport: Received pkt. pkthdr = %s. Frame header = %s.\n",
        pkthdr->to_string().c_str(),
        frame_header_to_string(&pkthdr->headroom[0]).c_str());

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

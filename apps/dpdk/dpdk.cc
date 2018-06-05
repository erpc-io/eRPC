#include "eth_helpers.h"
#include "util/rand.h"

void send_packets(struct rte_mempool *pktmbuf_pool, erpc::FastRand &fastrand) {
  rte_mbuf *tx_mbufs[kAppTxBatchSize];
  for (size_t i = 0; i < kAppTxBatchSize; i++) {
    tx_mbufs[i] = rte_pktmbuf_alloc(pktmbuf_pool);
    erpc::rt_assert(tx_mbufs[i] != nullptr);

    uint8_t *pkt = rte_pktmbuf_mtod(tx_mbufs[i], uint8_t *);

    auto *eth_hdr = reinterpret_cast<ether_hdr *>(pkt);
    auto *ip_hdr = reinterpret_cast<ipv4_hdr *>(pkt + sizeof(ether_hdr));

    memset(eth_hdr->s_addr.addr_bytes, 0, 6);
    memset(eth_hdr->d_addr.addr_bytes, 0, 6);
    eth_hdr->ether_type = htons(ETHER_TYPE_IPv4);

    ip_hdr->src_addr = fastrand.next_u32();
    ip_hdr->dst_addr = fastrand.next_u32();
    ip_hdr->version_ihl = 0x40 | 0x05;

    tx_mbufs[i]->nb_segs = 1;
    tx_mbufs[i]->pkt_len = 60;
    tx_mbufs[i]->data_len = 60;
  }

  size_t nb_tx_new =
      rte_eth_tx_burst(kAppPortId, kAppTxQueueId, tx_mbufs, kAppTxBatchSize);

  for (size_t i = nb_tx_new; i < kAppTxBatchSize; i++) {
    rte_pktmbuf_free(tx_mbufs[i]);
  }
}

void receive_packets() {
  struct rte_mbuf *rx_pkts[kAppRxBatchSize];
  size_t nb_rx = rte_eth_rx_burst(kAppPortId, 0, rx_pkts, kAppRxBatchSize);
  if (nb_rx > 0) printf("nb_rx = %zu\n", nb_rx);
}

int main() {
  erpc::FastRand fastrand;

  const char *argv[] = {"rc", "-c", "1", "-n", "1", nullptr};
  int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0])) - 1;
  int ret = rte_eal_init(argc, const_cast<char **>(argv));
  erpc::rt_assert(ret >= 0, "rte_eal_init failed");
  printf("DPDK initialized\n");

  uint16_t num_ports = rte_eth_dev_count_avail();
  printf("%u DPDK ports available\n", num_ports);
  erpc::rt_assert(num_ports > kAppPortId, "Too few ports");

  rte_mempool *pktmbuf_pool =
      rte_pktmbuf_pool_create("mbuf_pool", kAppNumMbufs, kAppNumCacheMbufs, 0,
                              kAppMbufSize, kAppNumaNode);

  erpc::rt_assert(pktmbuf_pool != nullptr,
                  "Failed to create mempool. Error = " +
                      std::string(rte_strerror(rte_errno)));

  struct ether_addr mac;
  rte_eth_macaddr_get(kAppPortId, &mac);
  printf("Ether addr = %s\n", mac_to_string(mac.addr_bytes).c_str());

  // configure some default NIC port parameters
  struct rte_eth_conf port_conf;
  memset(&port_conf, 0, sizeof(port_conf));
  port_conf.rxmode.max_rx_pkt_len = ETHER_MAX_VLAN_FRAME_LEN;
  rte_eth_dev_configure(kAppPortId, kAppNumRxQueues, kAppNumTxQueues,
                        &port_conf);

  struct rte_eth_txconf *tx_conf = nullptr;
  struct rte_eth_rxconf *rx_conf = nullptr;
  rte_eth_rx_queue_setup(kAppPortId, kAppRxQueueId, kAppNumRingDesc,
                         kAppNumaNode, rx_conf, pktmbuf_pool);
  rte_eth_tx_queue_setup(kAppPortId, kAppTxQueueId, kAppNumRingDesc,
                         kAppNumaNode, tx_conf);

  ret = rte_eth_dev_set_mtu(kAppPortId, kAppMTU);
  erpc::rt_assert(ret >= 0, "Failed to set MTU");

  ret = rte_eth_dev_start(kAppPortId);  // This starts the RX/TX queues
  erpc::rt_assert(ret >= 0, "Failed to start port");

  // Retrieve the link speed and compute information based on it.
  struct rte_eth_link link;
  rte_eth_link_get(kAppPortId, &link);
  erpc::rt_assert(link.link_status > 0, "Failed to detect link");
  erpc::rt_assert(link.link_speed != ETH_SPEED_NUM_NONE, "Failed to get bw");
  printf("Link bandwidth = %u Mbps\n", link.link_speed);

  rte_eth_promiscuous_enable(kAppPortId);

  while (true) {
    send_packets(pktmbuf_pool, fastrand);
    receive_packets();
  }
}

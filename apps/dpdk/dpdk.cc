#include <rte_common.h>
#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <rte_ring.h>
#include <rte_version.h>
#include <string>
#include "common.h"
#include "eth_helpers.h"
#include "util/rand.h"

#include <gflags/gflags.h>
DEFINE_uint64(is_sender, 0, "is_sender");

static constexpr size_t kAppMTU = 1024;
static constexpr size_t kAppPortId = 0;

static constexpr size_t kAppNumRingDesc = 256;

static constexpr size_t kAppRxBatchSize = 32;
static constexpr size_t kAppTxBatchSize = 32;
static constexpr size_t kAppDataSize = 16;  // App-level data size

// Maximum number of packet buffers that the memory pool can hold. The
// documentation of `rte_mempool_create` suggests that the optimum value
// (in terms of memory usage) of this number is a power of two minus one.
static constexpr size_t kAppNumMbufs = 8191;
static constexpr size_t kAppNumCacheMbufs = 32;

static constexpr size_t kAppNumaNode = 0;
static constexpr size_t kAppNumRxQueues = 1;
static constexpr size_t kAppNumTxQueues = 1;

static constexpr size_t kAppRxQueueId = 0;
static constexpr size_t kAppTxQueueId = 0;

uint8_t kDstMAC[6] = {0x3c, 0xfd, 0xfe, 0x56, 0x07, 0x42};
char kDstIP[] = "10.10.1.1";

uint8_t kSrcMAC[6] = {0x3c, 0xfd, 0xfe, 0x56, 0x19, 0x82};
char kSrcIP[] = "10.10.1.2";

// Per-element size for the packet buffer memory pool
static constexpr size_t kAppMbufSize =
    (2048 + static_cast<uint32_t>(sizeof(struct rte_mbuf)) +
     RTE_PKTMBUF_HEADROOM);

void send_packets(struct rte_mempool *pktmbuf_pool) {
  rte_mbuf *tx_mbufs[kAppTxBatchSize];
  for (size_t i = 0; i < kAppTxBatchSize; i++) {
    tx_mbufs[i] = rte_pktmbuf_alloc(pktmbuf_pool);
    erpc::rt_assert(tx_mbufs[i] != nullptr);

    uint8_t *pkt = rte_pktmbuf_mtod(tx_mbufs[i], uint8_t *);

    // For now, don't use DPDK's header defines
    auto *eth_hdr = reinterpret_cast<eth_hdr_t *>(pkt);
    auto *ip_hdr = reinterpret_cast<ipv4_hdr_t *>(pkt + sizeof(eth_hdr_t));
    auto *udp_hdr = reinterpret_cast<udp_hdr_t *>(pkt + sizeof(eth_hdr_t) +
                                                  sizeof(ipv4_hdr_t));

    gen_eth_header(eth_hdr, kSrcMAC, kDstMAC);
    gen_ipv4_header(ip_hdr, ip_from_str(kSrcIP), ip_from_str(kDstIP),
                    kAppDataSize);
    gen_udp_header(udp_hdr, 31850, 31850, kAppDataSize);

    tx_mbufs[i]->nb_segs = 1;
    tx_mbufs[i]->pkt_len = kTotHdrSz + kAppDataSize;
    tx_mbufs[i]->data_len = tx_mbufs[i]->pkt_len;
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

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  const char *rte_argv[] = {"rc", "-c", "1", "-n", "1", nullptr};
  int rte_argc = static_cast<int>(sizeof(argv) / sizeof(argv[0])) - 1;
  int ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
  erpc::rt_assert(ret >= 0, "rte_eal_init failed");

  uint16_t num_ports = rte_eth_dev_count_avail();
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
    FLAGS_is_sender == 1 ? send_packets(pktmbuf_pool) : receive_packets();
  }
}

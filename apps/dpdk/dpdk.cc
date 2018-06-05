#include "common.h"

#include <rte_common.h>
#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <rte_ring.h>
#include <rte_version.h>

static std::string mac_to_string(const uint8_t* mac) {
  std::ostringstream ret;
  for (size_t i = 0; i < 6; i++) {
    ret << std::hex << static_cast<uint32_t>(mac[i]);
    if (i != 5) ret << ":";
  }
  return ret.str();
}

static constexpr size_t kAppMTU = 1024;
static constexpr size_t kAppPortId = 0;

// Number of descriptors to allocate for the tx/rx rings
static constexpr size_t kAppNumRingDesc = 256;

// Maximum number of packet buffers that the memory pool can hold. The
// documentation of `rte_mempool_create` suggests that the optimum value
// (in terms of memory usage) of this number is a power of two minus one.
static constexpr size_t kAppNumMbufs = 8191;
static constexpr size_t kAppNumCacheMbufs = 32;

static constexpr size_t kAppNumaNode = 0;
static constexpr size_t kAppNumRxQueues = 1;
static constexpr size_t kAppNumTxQueues = 1;

// Per-element size for the packet buffer memory pool
static constexpr size_t kAppMbufSize =
    (2048 + static_cast<uint32_t>(sizeof(struct rte_mbuf)) +
     RTE_PKTMBUF_HEADROOM);

int main() {
  const char* argv[] = {"rc", "-c", "1", "-n", "1", nullptr};
  int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0])) - 1;
  int ret = rte_eal_init(argc, const_cast<char**>(argv));
  erpc::rt_assert(ret >= 0, "rte_eal_init failed");
  printf("DPDK initialized\n");

  uint16_t num_ports = rte_eth_dev_count_avail();
  printf("%u DPDK ports available\n", num_ports);
  erpc::rt_assert(num_ports > kAppPortId, "Too few ports");

  rte_mempool* pktmbuf_pool =
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

  // Set up a NIC/HW-based filter on the ethernet type so that only
  // traffic to a particular port is received by this driver.
  struct rte_eth_ethertype_filter filter;
  ret = rte_eth_dev_filter_supported(kAppPortId, RTE_ETH_FILTER_ETHERTYPE);
  erpc::rt_assert(ret >= 0, "Ethertype filter not supported");

  memset(&filter, 0, sizeof(filter));
  ret = rte_eth_dev_filter_ctrl(kAppPortId, RTE_ETH_FILTER_ETHERTYPE,
                                RTE_ETH_FILTER_ADD, &filter);
  erpc::rt_assert(ret >= 0, "Failed to add ethertype filter");

  struct rte_eth_txconf* tx_conf = nullptr;
  struct rte_eth_rxconf* rx_conf = nullptr;
  uint16_t rx_queue_id = 0, tx_queue_id = 0;
  rte_eth_rx_queue_setup(kAppPortId, rx_queue_id, kAppNumRingDesc, kAppNumaNode,
                         rx_conf, pktmbuf_pool);
  rte_eth_tx_queue_setup(kAppPortId, tx_queue_id, kAppNumRingDesc, kAppNumaNode,
                         tx_conf);

  ret = rte_eth_dev_set_mtu(kAppPortId, kAppMTU);
  erpc::rt_assert(ret >= 0, "Failed to set MTU");

  ret = rte_eth_dev_start(kAppPortId);
  erpc::rt_assert(ret >= 0, "Failed to start port");

  // Retrieve the link speed and compute information based on it.
  struct rte_eth_link link;
  rte_eth_link_get(kAppPortId, &link);
  erpc::rt_assert(link.link_status > 0, "Failed to detect link");
  erpc::rt_assert(link.link_speed != ETH_SPEED_NUM_NONE, "Failed to get bw");
  printf("Link bandwidth = %u Mbps\n", link.link_speed);
}

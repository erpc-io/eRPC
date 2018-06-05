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
  rte_eth_dev_configure(kAppPortId, 1, 1, &port_conf);

  // Set up a NIC/HW-based filter on the ethernet type so that only
  // traffic to a particular port is received by this driver.
  struct rte_eth_ethertype_filter filter;
  ret = rte_eth_dev_filter_supported(kAppPortId, RTE_ETH_FILTER_ETHERTYPE);
  erpc::rt_assert(ret >= 0, "Ethertype filter not supported");

  memset(&filter, 0, sizeof(filter));
  ret = rte_eth_dev_filter_ctrl(kAppPortId, RTE_ETH_FILTER_ETHERTYPE,
                                RTE_ETH_FILTER_ADD, &filter);
  erpc::rt_assert(ret >= 0, "Failed to add ethertype filter");

  rte_eth_rx_queue_setup(kAppPortId, 0, kAppNumRingDesc, 0, nullptr,
                         pktmbuf_pool);
  rte_eth_tx_queue_setup(kAppPortId, 0, kAppNumRingDesc, 0, nullptr);

  ret = rte_eth_dev_set_mtu(kAppPortId, kAppMTU);
  erpc::rt_assert(ret >= 0, "Failed to set MTU");

  ret = rte_eth_dev_start(kAppPortId);
  erpc::rt_assert(ret >= 0, "Failed to start port");

  printf("Success\n");

  /*
  // Retrieve the link speed and compute information based on it.
  struct rte_eth_link link;
  rte_eth_link_get(kAppPortId, &link);
  if (!link.link_status) {
    throw DriverException(
        HERE, format("Failed to detect a link on Ethernet port %u",
  kAppPortId));
  }
  if (link.link_speed != ETH_SPEED_NUM_NONE) {
    // Be conservative about the link speed. We use bandwidth in
    // QueueEstimator to estimate # bytes outstanding in the NIC's
    // TX queue. If we overestimate the bandwidth, under high load,
    // we may keep queueing packets faster than the NIC can consume,
    // and build up a queue in the TX queue.
    bandwidthMbps = (uint32_t)(link.link_speed * 0.98);
  } else {
    LOG(WARNING,
        "Can't retrieve network bandwidth from DPDK; "
        "using default of %d Mbps",
        bandwidthMbps);
  }
  queueEstimator.setBandwidth(bandwidthMbps);
  maxTransmitQueueSize =
      (uint32_t)(static_cast<double>(bandwidthMbps) * MAX_DRAIN_TIME / 8000.0);
  uint32_t maxPacketSize = getMaxPacketSize();
  if (maxTransmitQueueSize < 2 * maxPacketSize) {
    // Make sure that we advertise enough space in the transmit queue to
    // prepare the next packet while the current one is transmitting.
    maxTransmitQueueSize = 2 * maxPacketSize;
  }

  // create an in-memory ring, used as a software loopback in order to handle
  // packets that are addressed to the localhost.
  loopbackRing = rte_ring_create("dpdk_loopback_ring", 4096, SOCKET_ID_ANY, 0);
  if (nullptr == loopbackRing) {
    throw DriverException(HERE, format("Failed to allocate loopback ring: %s",
                                       rte_strerror(rte_errno)));
  }

  LOG(NOTICE,
      "DpdkDriver locator: %s, bandwidth: %d Mbits/sec, "
      "maxTransmitQueueSize: %u bytes",
      locatorString.c_str(), bandwidthMbps, maxTransmitQueueSize);

  // DPDK during initialization (rte_eal_init()) pins the running thread
  // to a single processor. This becomes a problem as the master worker
  // threads are created after the initialization of the transport, and
  // thus inherit the (very) restricted affinity to a single core. This
  // essentially kills performance, as every thread is contenting for a
  // single core. Revert this, by restoring the affinity to the default
  // (all cores).
  Util::clearCpuAffinity();
  */
}

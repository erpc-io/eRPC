#include "common.h"

#define __STDC_LIMIT_MACROS
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#pragma GCC diagnostic ignored "-Wconversion"
#include <rte_common.h>
#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <rte_ring.h>
#include <rte_version.h>
#pragma GCC diagnostic warning "-Wconversion"

static constexpr size_t kAppPortId = 0;

int main() {
  const char* argv[] = {"rc", "-c", "1", "-n", "1", NULL};
  int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0])) - 1;
  int ret = rte_eal_init(argc, const_cast<char**>(argv));
  erpc::rt_assert(ret >= 0, "rte_eal_init failed");
  printf("DPDK initialized\n");

  uint16_t num_ports = rte_eth_dev_count();
  printf("%u DPDK ports available\n", num_ports);
  erpc::rt_assert(num_ports > kAppPortId, "Too few ports");

  /*
  struct ether_addr mac;
  struct rte_eth_conf portConf;


  // create an memory pool for accommodating packet buffers
  mbufPool = rte_mempool_create("mbuf_pool", NB_MBUF, MBUF_SIZE, 32,
                                sizeof32(struct rte_pktmbuf_pool_private),
                                rte_pktmbuf_pool_init, NULL, rte_pktmbuf_init,
                                NULL, rte_socket_id(), 0);

  if (!mbufPool) {
    throw DriverException(
        HERE, format("Failed to allocate memory for packet buffers: %s",
                     rte_strerror(rte_errno)));
  }

  // Read the MAC address from the NIC via DPDK.
  rte_eth_macaddr_get(portId, &mac);
  localMac.construct(mac.addr_bytes);
  locatorString = format("dpdk:mac=%s", localMac->toString().c_str());

  // configure some default NIC port parameters
  memset(&portConf, 0, sizeof(portConf));
  portConf.rxmode.max_rx_pkt_len = ETHER_MAX_VLAN_FRAME_LEN;
  rte_eth_dev_configure(portId, 1, 1, &portConf);

  // Set up a NIC/HW-based filter on the ethernet type so that only
  // traffic to a particular port is received by this driver.
  struct rte_eth_ethertype_filter filter;
  ret = rte_eth_dev_filter_supported(portId, RTE_ETH_FILTER_ETHERTYPE);
  if (ret < 0) {
    LOG(NOTICE, "ethertype filter is not supported on port %u.", portId);
    hasHardwareFilter = false;
  } else {
    memset(&filter, 0, sizeof(filter));
    ret = rte_eth_dev_filter_ctrl(portId, RTE_ETH_FILTER_ETHERTYPE,
                                  RTE_ETH_FILTER_ADD, &filter);
    if (ret < 0) {
      LOG(WARNING, "failed to add ethertype filter\n");
      hasHardwareFilter = false;
    }
  }

  // setup and initialize the receive and transmit NIC queues,
  // and activate the port.
  rte_eth_rx_queue_setup(portId, 0, NDESC, 0, NULL, mbufPool);
  rte_eth_tx_queue_setup(portId, 0, NDESC, 0, NULL);

  // set the MTU that the NIC port should support
  ret = rte_eth_dev_set_mtu(portId, MAX_PAYLOAD_SIZE);
  if (ret != 0) {
    throw DriverException(
        HERE, format("Failed to set the MTU on Ethernet port  %u: %s", portId,
                     rte_strerror(rte_errno)));
  }

  ret = rte_eth_dev_start(portId);
  if (ret != 0) {
    throw DriverException(HERE, format("Couldn't start port %u, error %d (%s)",
                                       portId, ret, strerror(ret)));
  }

  // Retrieve the link speed and compute information based on it.
  struct rte_eth_link link;
  rte_eth_link_get(portId, &link);
  if (!link.link_status) {
    throw DriverException(
        HERE, format("Failed to detect a link on Ethernet port %u", portId));
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
  if (NULL == loopbackRing) {
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

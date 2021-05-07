#ifdef ERPC_DPDK

#include <iomanip>
#include <stdexcept>

#include <rte_thash.h>
#include <rte_version.h>
#include <set>
#include "dpdk_externs.h"
#include "dpdk_transport.h"
#include "util/huge_alloc.h"

namespace erpc {

constexpr size_t DpdkTransport::kMaxDataPerPkt;
static_assert(sizeof(eth_routing_info_t) <= Transport::kMaxRoutingInfoSize, "");

static_assert(kHeadroom == 40, "Invalid packet header headroom for DPDK");
static_assert(sizeof(pkthdr_t::headroom) == kInetHdrsTotSize, "Wrong headroom");

// Initialize the protection domain, queue pair, and memory registration and
// deregistration functions. RECVs will be initialized later when the hugepage
// allocator is provided.
DpdkTransport::DpdkTransport(uint16_t sm_udp_port, uint8_t rpc_id,
                             uint8_t phy_port, size_t numa_node,
                             FILE *trace_file)
    : Transport(TransportType::kDPDK, rpc_id, phy_port, numa_node, trace_file) {
  // For DPDK, we compute the datapath UDP port using the physical port and Rpc
  // ID, so we don't need sm_udp_port like Raw transport.
  _unused(sm_udp_port);

  {
    // The first thread to grab the lock initializes DPDK
    g_dpdk_lock.lock();

    // Get an available queue on phy_port. This does not require phy_port to
    // be initialized.
    rt_assert(g_used_qp_ids[phy_port].size() < kMaxQueuesPerPort,
              "No queues left on port " + std::to_string(phy_port));
    for (size_t i = 0; i < kMaxQueuesPerPort; i++) {
      if (g_used_qp_ids[phy_port].count(i) == 0) {
        qp_id = i;
        rx_flow_udp_port = udp_port_for_queue(phy_port, qp_id);
        break;
      }
    }
    g_used_qp_ids[phy_port].insert(qp_id);

    if (g_dpdk_initialized) {
      ERPC_INFO(
          "DPDK transport for Rpc %u skipping initialization, queue ID = %zu\n",
          rpc_id, qp_id);
    } else {
      ERPC_INFO("DPDK transport for Rpc %u initializing DPDK, queue ID = %zu\n",
                rpc_id, qp_id);

      // n: channels, m: maximum memory in megabytes
      const char *rte_argv[] = {
          "-c",          "1",
          "-n",          "6",
          "--log-level", (ERPC_LOG_LEVEL >= ERPC_LOG_LEVEL_INFO) ? "8" : "0",
          "-m",          "1024",
          nullptr};

      int rte_argc =
          static_cast<int>(sizeof(rte_argv) / sizeof(rte_argv[0])) - 1;
      int ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
      rt_assert(ret >= 0, "Failed to initialize DPDK");

      g_dpdk_initialized = true;
    }

    if (!g_port_initialized[phy_port]) {
      g_port_initialized[phy_port] = true;
      setup_phy_port();
    }

    // Here, mempools for phy_port have been initialized
    mempool = g_mempool_arr[phy_port][qp_id];

    g_dpdk_lock.unlock();
  }

  resolve_phy_port();
  init_mem_reg_funcs();

  ERPC_WARN(
      "DpdkTransport created for Rpc ID %u, queue %zu, datapath UDP port %u\n",
      rpc_id, qp_id, rx_flow_udp_port);
}

void DpdkTransport::init_hugepage_structures(HugeAlloc *huge_alloc,
                                             uint8_t **rx_ring) {
  this->huge_alloc = huge_alloc;
  this->rx_ring = rx_ring;
}

DpdkTransport::~DpdkTransport() {
  ERPC_INFO("Destroying transport for ID %u\n", rpc_id);

  rte_mempool_free(mempool);

  {
    g_dpdk_lock.lock();
    g_used_qp_ids[phy_port].erase(g_used_qp_ids[phy_port].find(qp_id));
    g_dpdk_lock.unlock();
  }
}

void DpdkTransport::resolve_phy_port() {
  struct rte_ether_addr mac;
  rte_eth_macaddr_get(phy_port, &mac);
  memcpy(resolve.mac_addr, &mac.addr_bytes, sizeof(resolve.mac_addr));

  resolve.ipv4_addr = get_port_ipv4_addr(phy_port);

  // Resolve RSS indirection table size
  struct rte_eth_dev_info dev_info;
  rte_eth_dev_info_get(phy_port, &dev_info);

  rt_assert(std::string(dev_info.driver_name) == "net_mlx4" or
                std::string(dev_info.driver_name) == "net_mlx5",
            "eRPC supports only mlx4 or mlx5 devices with DPDK");
  if (std::string(dev_info.driver_name) == "net_mlx4") {
    // MLX4 NICs report a reta size of zero, but they use 128 internally
    rt_assert(dev_info.reta_size == 0,
              "Unexpected RETA size for MLX4 NIC (expected zero)");
    resolve.reta_size = 128;
  } else {
    resolve.reta_size = dev_info.reta_size;
    rt_assert(resolve.reta_size >= kMaxQueuesPerPort,
              "Too few entries in NIC RSS indirection table");
  }

  // Resolve bandwidth
  struct rte_eth_link link;
  rte_eth_link_get(static_cast<uint8_t>(phy_port), &link);
  rt_assert(link.link_status == ETH_LINK_UP,
            "Port " + std::to_string(phy_port) + " is down.");

  if (link.link_speed != ETH_SPEED_NUM_NONE) {
    // link_speed is in Mbps. The 10 Gbps check below is just a sanity check.
    rt_assert(link.link_speed >= 10000, "Link too slow");
    resolve.bandwidth =
        static_cast<size_t>(link.link_speed) * 1000 * 1000 / 8.0;
  } else {
    ERPC_WARN(
        "Port %u bandwidth not reported by DPDK. Using default 10 Gbps.\n",
        phy_port);
    link.link_speed = 10000;
    resolve.bandwidth = 10.0 * (1000 * 1000 * 1000) / 8.0;
  }

  ERPC_INFO(
      "Resolved port %u: MAC %s, IPv4 %s, RETA size %zu entries, bandwidth "
      "%.1f Gbps\n",
      phy_port, mac_to_string(resolve.mac_addr).c_str(),
      ipv4_to_string(htonl(resolve.ipv4_addr)).c_str(), resolve.reta_size,
      resolve.bandwidth * 8.0 / (1000 * 1000 * 1000));
}

void DpdkTransport::fill_local_routing_info(RoutingInfo *routing_info) const {
  memset(static_cast<void *>(routing_info), 0, kMaxRoutingInfoSize);
  auto *ri = reinterpret_cast<eth_routing_info_t *>(routing_info);
  memcpy(ri->mac, resolve.mac_addr, 6);
  ri->ipv4_addr = resolve.ipv4_addr;
  ri->udp_port = rx_flow_udp_port;
  ri->rxq_id = qp_id;
  ri->reta_size = resolve.reta_size;
}

// Generate most fields of the L2--L4 headers now to avoid recomputation.
bool DpdkTransport::resolve_remote_routing_info(
    RoutingInfo *routing_info) const {
  auto *ri = reinterpret_cast<eth_routing_info_t *>(routing_info);

  // XXX: The header generation below will overwrite routing_info. We must
  // save/use info from routing_info before that.
  uint8_t remote_mac[6];
  memcpy(remote_mac, ri->mac, 6);
  const uint32_t remote_ipv4_addr = ri->ipv4_addr;
  const uint16_t remote_udp_port = ri->udp_port;

  uint16_t i = kBaseEthUDPPort;
  for (; i < UINT16_MAX; i++) {
    union rte_thash_tuple tuple;
    tuple.v4.src_addr = resolve.ipv4_addr;
    tuple.v4.dst_addr = remote_ipv4_addr;
    tuple.v4.sport = i;
    tuple.v4.dport = remote_udp_port;
    uint32_t rss_l3l4 = rte_softrss(reinterpret_cast<uint32_t *>(&tuple),
                                    RTE_THASH_V4_L4_LEN, kDefaultRssKey);
    if ((rss_l3l4 % ri->reta_size) % DpdkTransport::kMaxQueuesPerPort ==
        ri->rxq_id)
      break;
  }
  rt_assert(i < UINT16_MAX, "Scan error");

  // Overwrite routing_info by constructing the packet header in place
  static_assert(kMaxRoutingInfoSize >= kInetHdrsTotSize, "");

  auto *eth_hdr = reinterpret_cast<eth_hdr_t *>(ri);
  gen_eth_header(eth_hdr, &resolve.mac_addr[0], remote_mac);

  auto *ipv4_hdr = reinterpret_cast<ipv4_hdr_t *>(&eth_hdr[1]);
  gen_ipv4_header(ipv4_hdr, resolve.ipv4_addr, remote_ipv4_addr, 0);

  auto *udp_hdr = reinterpret_cast<udp_hdr_t *>(&ipv4_hdr[1]);
  gen_udp_header(udp_hdr, i, remote_udp_port, 0);

  return true;
}

/// A dummy memory registration function
static Transport::MemRegInfo dpdk_reg_mr_wrapper(void *, size_t) {
  return Transport::MemRegInfo();
}

/// A dummy memory de-registration function
static void dpdk_dereg_mr_wrapper(Transport::MemRegInfo) { return; }

void DpdkTransport::init_mem_reg_funcs() {
  using namespace std::placeholders;
  reg_mr_func = std::bind(dpdk_reg_mr_wrapper, _1, _2);
  dereg_mr_func = std::bind(dpdk_dereg_mr_wrapper, _1);
}
}  // namespace erpc

#endif

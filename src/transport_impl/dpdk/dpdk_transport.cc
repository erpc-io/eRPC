#include <iomanip>
#include <stdexcept>

#include "dpdk_transport.h"
#include "util/huge_alloc.h"

namespace erpc {

constexpr size_t DpdkTransport::kMaxDataPerPkt;

// Initialize the protection domain, queue pair, and memory registration and
// deregistration functions. RECVs will be initialized later when the hugepage
// allocator is provided.
DpdkTransport::DpdkTransport(uint8_t rpc_id, uint8_t phy_port, size_t numa_node,
                             FILE *trace_file)
    : Transport(TransportType::kDPDK, rpc_id, phy_port, numa_node, trace_file),
      rx_flow_udp_port(kBaseEthUDPPort + (256u * numa_node) + rpc_id) {
  rt_assert(kHeadroom == 40, "Invalid packet header headroom for raw Ethernet");
  rt_assert(sizeof(pkthdr_t::headroom) == kInetHdrsTotSize, "Invalid headroom");

  static std::mutex eal_init_lock;
  static size_t eal_initialized;
  static size_t avialable_qp_id;

  {
    eal_init_lock.lock();
    if (eal_initialized == 1) {
      LOG_INFO("Rpc %u skipping DPDK initialization, queues ID = %zu.\n",
               rpc_id, avialable_qp_id);
      qp_id = avialable_qp_id++;
    } else {
      LOG_INFO("Rpc %u initializing DPDK, queues ID = %zu.\n", rpc_id,
               avialable_qp_id);
      do_per_process_dpdk_init();

      eal_initialized = true;
      qp_id = avialable_qp_id++;
    }

    eal_init_lock.unlock();
  }

  resolve_phy_port();
  init_mempool_and_queues();
  install_flow_rule();
  init_mem_reg_funcs();

  LOG_WARN("DpdkTransport created for ID %u", rpc_id);
}

void DpdkTransport::init_hugepage_structures(HugeAlloc *huge_alloc,
                                             uint8_t **rx_ring) {
  this->huge_alloc = huge_alloc;
  this->rx_ring = rx_ring;
}

// The transport destructor is called after \p huge_alloc has already been
// destroyed by \p Rpc. Deleting \p huge_alloc deregisters and frees all SHM
// memory regions.
//
// We only need to clean up non-hugepage structures.
DpdkTransport::~DpdkTransport() {
  LOG_INFO("Destroying transport for ID %u\n", rpc_id);

  rte_mempool_free(mempool);
}

void DpdkTransport::init_mempool_and_queues() {
  std::string pname = "mempool-rpc-" + std::to_string(rpc_id);
  printf("Creating mempool %s\n", pname.c_str());

  mempool = rte_pktmbuf_pool_create(pname.c_str(), kNumMbufs, 0 /* cache */,
                                    0 /* headroom */, kMbufSize, numa_node);
  rt_assert(mempool != nullptr,
            "Mempool create failed: " + std::string(rte_strerror(rte_errno)));

  rte_eth_rxconf eth_rx_conf;
  memset(&eth_rx_conf, 0, sizeof(eth_rx_conf));
  eth_rx_conf.rx_thresh.pthresh = 8;
  eth_rx_conf.rx_thresh.hthresh = 0;
  eth_rx_conf.rx_thresh.wthresh = 0;
  eth_rx_conf.rx_free_thresh = 0;
  eth_rx_conf.rx_drop_en = 0;

  int ret = rte_eth_rx_queue_setup(phy_port, qp_id, kNumRxRingEntries,
                                   numa_node, &eth_rx_conf, mempool);
  rt_assert(ret == 0, "Failed to setup RX queue: " + std::to_string(qp_id));

  rte_eth_txconf eth_tx_conf;
  memset(&eth_tx_conf, 0, sizeof(eth_tx_conf));
  eth_tx_conf.tx_thresh.pthresh = 32;
  eth_tx_conf.tx_thresh.hthresh = 0;
  eth_tx_conf.tx_thresh.wthresh = 0;
  eth_tx_conf.tx_free_thresh = 0;
  eth_tx_conf.tx_rs_thresh = 0;
  eth_tx_conf.txq_flags = ETH_TXQ_FLAGS_IGNORE;  // Use offloads below instead
  eth_tx_conf.offloads = kOffloads;
  ret = rte_eth_tx_queue_setup(phy_port, qp_id, kNumTxRingDesc, numa_node,
                               &eth_tx_conf);
  rt_assert(ret == 0, "Failed to setup TX queue: " + std::to_string(qp_id));
}

void DpdkTransport::resolve_phy_port() {
  struct ether_addr mac;
  rte_eth_macaddr_get(phy_port, &mac);
  memcpy(resolve.mac_addr, &mac.addr_bytes, sizeof(resolve.mac_addr));

  resolve.ipv4_addr = ipv4_from_str(kTempIp);
}

void DpdkTransport::do_per_process_dpdk_init() {
  const char *rte_argv[] = {"-c", "1", "-n", "4", nullptr};
  int rte_argc = static_cast<int>(sizeof(rte_argv) / sizeof(rte_argv[0])) - 1;
  int ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
  rt_assert(ret >= 0, "rte_eal_init failed");

  uint16_t num_ports = rte_eth_dev_count_avail();
  rt_assert(num_ports > phy_port, "Too few ports");

  rte_eth_dev_info dev_info;
  rte_eth_dev_info_get(phy_port, &dev_info);
  rt_assert(dev_info.rx_desc_lim.nb_max >= kNumRxRingEntries,
            "Device RX ring too small");

  // Create per-thread RX and TX queues
  rte_eth_conf eth_conf;
  memset(&eth_conf, 0, sizeof(eth_conf));

  eth_conf.rxmode.mq_mode = ETH_MQ_RX_NONE;
  eth_conf.rxmode.max_rx_pkt_len = ETHER_MAX_LEN;
  eth_conf.rxmode.ignore_offload_bitfield = 1;  // Use offloads below instead
  eth_conf.rxmode.offloads = 0;

  eth_conf.txmode.mq_mode = ETH_MQ_TX_NONE;
  eth_conf.txmode.offloads = kOffloads;

  eth_conf.fdir_conf.mode = RTE_FDIR_MODE_PERFECT;
  eth_conf.fdir_conf.pballoc = RTE_FDIR_PBALLOC_64K;
  eth_conf.fdir_conf.status = RTE_FDIR_NO_REPORT_STATUS;
  eth_conf.fdir_conf.mask.dst_port_mask = 0xffff;
  eth_conf.fdir_conf.drop_queue = 0;

  ret = rte_eth_dev_configure(phy_port, kMaxQueues, kMaxQueues, &eth_conf);
  rt_assert(ret == 0, "Dev config err " + std::string(rte_strerror(rte_errno)));

  // FILTER_SET fails for ixgbe, even though it supports flow director. As a
  // workaround, don't call FILTER_SET if ntuple filter is supported.
  if (rte_eth_dev_filter_supported(phy_port, RTE_ETH_FILTER_NTUPLE) != 0) {
    struct rte_eth_fdir_filter_info fi;
    memset(&fi, 0, sizeof(fi));
    fi.info_type = RTE_ETH_FDIR_FILTER_INPUT_SET_SELECT;
    fi.info.input_set_conf.flow_type = RTE_ETH_FLOW_NONFRAG_IPV4_UDP;
    fi.info.input_set_conf.inset_size = 2;
    fi.info.input_set_conf.field[0] = RTE_ETH_INPUT_SET_L3_DST_IP4;
    fi.info.input_set_conf.field[1] = RTE_ETH_INPUT_SET_L4_UDP_DST_PORT;
    fi.info.input_set_conf.op = RTE_ETH_INPUT_SET_SELECT;
    ret = rte_eth_dev_filter_ctrl(phy_port, RTE_ETH_FILTER_FDIR,
                                  RTE_ETH_FILTER_SET, &fi);
    rt_assert(ret == 0, "Failed to configure flow director fields");
  }

  // XXX: Are these thresh and txq_flags value optimal?
  rte_eth_rxconf eth_rx_conf;
  memset(&eth_rx_conf, 0, sizeof(eth_rx_conf));
  eth_rx_conf.rx_thresh.pthresh = 8;
  eth_rx_conf.rx_thresh.hthresh = 0;
  eth_rx_conf.rx_thresh.wthresh = 0;
  eth_rx_conf.rx_free_thresh = 0;
  eth_rx_conf.rx_drop_en = 0;

  rte_eth_txconf eth_tx_conf;
  memset(&eth_tx_conf, 0, sizeof(eth_tx_conf));
  eth_tx_conf.tx_thresh.pthresh = 32;
  eth_tx_conf.tx_thresh.hthresh = 0;
  eth_tx_conf.tx_thresh.wthresh = 0;
  eth_tx_conf.tx_free_thresh = 0;
  eth_tx_conf.tx_rs_thresh = 0;
  eth_tx_conf.txq_flags = ETH_TXQ_FLAGS_IGNORE;  // Use offloads below instead
  eth_tx_conf.offloads = eth_conf.txmode.offloads;
}

void DpdkTransport::fill_local_routing_info(RoutingInfo *routing_info) const {
  memset(static_cast<void *>(routing_info), 0, kMaxRoutingInfoSize);
  auto *ri = reinterpret_cast<eth_routing_info_t *>(routing_info);
  memcpy(ri->mac, resolve.mac_addr, 6);
  ri->ipv4_addr = resolve.ipv4_addr;
  ri->udp_port = rx_flow_udp_port;
}

// Generate most fields of the L2--L4 headers now to avoid recomputation.
bool DpdkTransport::resolve_remote_routing_info(
    RoutingInfo *routing_info) const {
  auto *ri = reinterpret_cast<eth_routing_info_t *>(routing_info);
  uint8_t remote_mac[6];
  memcpy(remote_mac, ri->mac, 6);
  uint32_t remote_ipv4_addr = ri->ipv4_addr;
  uint16_t remote_udp_port = ri->udp_port;

  static_assert(kMaxRoutingInfoSize >= kInetHdrsTotSize, "");

  auto *eth_hdr = reinterpret_cast<eth_hdr_t *>(ri);
  gen_eth_header(eth_hdr, &resolve.mac_addr[0], remote_mac);

  auto *ipv4_hdr = reinterpret_cast<ipv4_hdr_t *>(&eth_hdr[1]);
  gen_ipv4_header(ipv4_hdr, resolve.ipv4_addr, remote_ipv4_addr, 0);

  auto *udp_hdr = reinterpret_cast<udp_hdr_t *>(&ipv4_hdr[1]);
  gen_udp_header(udp_hdr, rx_flow_udp_port, remote_udp_port, 0);
  return true;
}

void DpdkTransport::install_flow_rule() {
  // Install a rule for rx_flow_udp_port
  int ret;
  if (rte_eth_dev_filter_supported(phy_port, RTE_ETH_FILTER_NTUPLE) == 0) {
    // Use 5-tuple filter for ixgbe even though it technically supports
    // FILTER_FDIR. I couldn't get FILTER_FDIR to work with ixgbe.
    struct rte_eth_ntuple_filter ntuple;
    memset(&ntuple, 0, sizeof(ntuple));
    ntuple.flags = RTE_5TUPLE_FLAGS;
    ntuple.dst_port = rte_cpu_to_be_16(rx_flow_udp_port);
    ntuple.dst_port_mask = UINT16_MAX;
    ntuple.proto = IPPROTO_UDP;
    ntuple.proto_mask = UINT8_MAX;
    ntuple.priority = 1;
    ntuple.queue = qp_id;

    ret = rte_eth_dev_filter_ctrl(phy_port, RTE_ETH_FILTER_NTUPLE,
                                  RTE_ETH_FILTER_ADD, &ntuple);
  } else if (rte_eth_dev_filter_supported(phy_port, RTE_ETH_FILTER_FDIR) == 0) {
    // Use fdir filter for i40e (5-tuple not supported)
    rte_eth_fdir_filter filter;
    memset(&filter, 0, sizeof(filter));
    filter.soft_id = qp_id;
    filter.input.flow_type = RTE_ETH_FLOW_NONFRAG_IPV4_UDP;
    filter.input.flow.udp4_flow.dst_port = rte_cpu_to_be_16(rx_flow_udp_port);
    filter.input.flow.udp4_flow.ip.dst_ip = ipv4_from_str(kTempIp);
    filter.action.rx_queue = qp_id;
    filter.action.behavior = RTE_ETH_FDIR_ACCEPT;
    filter.action.report_status = RTE_ETH_FDIR_NO_REPORT_STATUS;

    ret = rte_eth_dev_filter_ctrl(qp_id, RTE_ETH_FILTER_FDIR,
                                  RTE_ETH_FILTER_ADD, &filter);
  } else {
    rt_assert(false, "No flow director filters supported");
    ret = -1;
  }

  rt_assert(ret == 0,
            "Failed to add fdir entry " + std::string(rte_strerror(errno)));
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
}  // End erpc

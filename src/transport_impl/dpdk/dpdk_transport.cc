#ifdef ERPC_DPDK

#include "dpdk_transport.h"
#include <iomanip>
#include <set>
#include <stdexcept>
#include "dpdk_externs.h"
#include "util/huge_alloc.h"
#include "util/numautils.h"

namespace erpc {

constexpr size_t DpdkTransport::kMaxDataPerPkt;
static_assert(sizeof(eth_routing_info_t) <= Transport::kMaxRoutingInfoSize, "");

static_assert(kHeadroom == 40, "Invalid packet header headroom for DPDK");
static_assert(sizeof(pkthdr_t::headroom_) == kInetHdrsTotSize,
              "Wrong headroom");

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

    if (g_dpdk_initialized) {
      ERPC_INFO("DPDK transport for Rpc %u skipping DPDK EAL initialization.\n",
                rpc_id);
    } else {
      ERPC_INFO("DPDK transport for Rpc %u initializing DPDK EAL.\n", rpc_id);

      // clang-format off
      const char *rte_argv[] = {
          "-c",            "0x0",
          "-n",            "6",  // Memory channels
          "-m",            "1024", // Max memory in megabytes
          "--proc-type",   "auto",
          "--log-level",   (ERPC_LOG_LEVEL >= ERPC_LOG_LEVEL_INFO) ? "8" : "0",
          nullptr};
      // clang-format on

      const int rte_argc =
          static_cast<int>(sizeof(rte_argv) / sizeof(rte_argv[0])) - 1;
      int ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
      rt_assert(ret >= 0, "Failed to initialize DPDK");

      // rte_eal_init() sets process core affinity to only core #0, undo this
      clear_affinity_for_process();

      dpdk_proc_type_ = ((rte_eal_process_type() == RTE_PROC_PRIMARY)
                             ? DpdkProcType::kPrimary
                             : DpdkProcType::kSecondary);

      if (dpdk_proc_type_ == DpdkProcType::kPrimary) {
        ERPC_WARN(
            "Running as primary DPDK process. eRPC DPDK daemon is not "
            "running.\n");

        // Create a fake memzone
        g_memzone = new ownership_memzone_t();
        g_memzone->init();
      } else {
        // We're running as a secondary process, but we need to ensure that the
        // primary process is in fact the daemon, and not a regular non-daemon
        // eRPC process.
        const std::string memzone_name = get_memzone_name();
        auto dpdk_memzone = rte_memzone_lookup(memzone_name.c_str());
        if (dpdk_memzone == nullptr) {
          ERPC_ERROR(
              "Memzone %s not found. This can happen if another non-daemon "
              "eRPC process is running.\n",
              memzone_name.c_str());
          exit(-1);
        }
        g_memzone = reinterpret_cast<ownership_memzone_t *>(dpdk_memzone->addr);

        ERPC_WARN(
            "Running as secondary DPDK process. eRPC DPDK daemon is "
            "running.\n");
        g_port_initialized[phy_port] = true;
      }

      g_dpdk_initialized = true;
    }

    // Get an available queue on phy_port
    qp_id_ = g_memzone->get_qp(phy_port, 33 /* XXX */);
    if (qp_id_ != kInvalidQpId) {
      ERPC_INFO("DPDK transport for Rpc %u got QP %zu\n", rpc_id, qp_id_);
    } else {
      ERPC_ERROR(
          "DPDK transport for Rpc %u failed to get a free TX/RQ queue pair. "
          "All %zu available queue pairs are in use by Rpc objects.\n",
          rpc_id, kMaxQueuesPerPort);
      throw std::runtime_error("Failed to get DPDK QP");
    }

    rx_flow_udp_port_ = kBaseEthUDPPort + qp_id_;
    const std::string mempool_name = get_mempool_name(phy_port, qp_id_);

    if (dpdk_proc_type_ == DpdkProcType::kSecondary) {
      // The eRPC DPDK management daemon has already initialized phy_port
      mempool_ = rte_mempool_lookup(mempool_name.c_str());
      rt_assert(mempool_ != nullptr,
                std::string("Failed to find eRPC DPDK daemon's mempool ") +
                    mempool_name.c_str());
      drain_rx_queue();

      const size_t n_avail = rte_mempool_avail_count(mempool_);
      if (n_avail < kNumMbufs) {
        ERPC_WARN(
            "DPDK transport for Rpc %u: Mempool has only %zu free mbufs "
            "out of %zu. %zu mbufs have been leaked by previous processes that "
            "owned this mempool.\n",
            rpc_id, n_avail, kNumMbufs, (kNumMbufs - n_avail));
      }
    } else {
      if (!g_port_initialized[phy_port]) {
        g_port_initialized[phy_port] = true;
        setup_phy_port(phy_port, numa_node, DpdkProcType::kPrimary);
      }

      mempool_ = rte_mempool_lookup(mempool_name.c_str());
      rt_assert(
          mempool_ != nullptr,
          std::string("Failed to find self's mempool ") + mempool_name.c_str());
    }

    g_dpdk_lock.unlock();
  }

  resolve_phy_port();
  init_mem_reg_funcs();

  ERPC_WARN(
      "DpdkTransport created for Rpc ID %u, queue %zu, datapath UDP port %u\n",
      rpc_id, qp_id_, rx_flow_udp_port_);
}

void DpdkTransport::init_hugepage_structures(HugeAlloc *huge_alloc,
                                             uint8_t **rx_ring) {
  this->huge_alloc_ = huge_alloc;
  this->rx_ring_ = rx_ring;
}

DpdkTransport::~DpdkTransport() {
  ERPC_INFO("Destroying transport for ID %u\n", rpc_id_);
  drain_rx_queue();

  // XXX: For now, leak mempool_
  // if (dpdk_proc_type_ == DpdkProcType::kPrimary) rte_mempool_free(mempool_);

  int ret = g_memzone->free_qp(phy_port_, qp_id_);
  rt_assert(ret == 0, "Failed to free QP\n");
}

void DpdkTransport::resolve_phy_port() {
  struct rte_ether_addr mac;
  rte_eth_macaddr_get(phy_port_, &mac);
  memcpy(resolve_.mac_addr_, &mac.addr_bytes, sizeof(resolve_.mac_addr_));

  resolve_.ipv4_addr_ = get_port_ipv4_addr(phy_port_);

  // Resolve RSS indirection table size
  struct rte_eth_dev_info dev_info;
  rte_eth_dev_info_get(phy_port_, &dev_info);

  const std::string drv_name = dev_info.driver_name;
  rt_assert(drv_name == "net_mlx4" or drv_name == "net_mlx5" or
                drv_name == "mlx5_pci",
            "eRPC supports only mlx4 or mlx5 devices with DPDK");

  if (std::string(dev_info.driver_name) == "net_mlx4") {
    // MLX4 NICs report a reta size of zero, but they use 128 internally
    rt_assert(dev_info.reta_size == 0,
              "Unexpected RETA size for MLX4 NIC (expected zero)");
    resolve_.reta_size_ = 128;
  } else {
    resolve_.reta_size_ = dev_info.reta_size;
    rt_assert(resolve_.reta_size_ >= kMaxQueuesPerPort,
              "Too few entries in NIC RSS indirection table");
  }

  // Resolve bandwidth. XXX: For some reason, rte_eth_link_get() does not work
  // in secondary DPDK processes in DPDK 19.11.
  struct rte_eth_link link;
  if (dpdk_proc_type_ == DpdkProcType::kPrimary) {
    rte_eth_link_get(static_cast<uint8_t>(phy_port_), &link);
    rt_assert(link.link_status == ETH_LINK_UP,
              "Port " + std::to_string(phy_port_) + " is down.");
  } else {
    link = g_memzone->link_[phy_port_];
  }

  if (link.link_speed != ETH_SPEED_NUM_NONE) {
    // link_speed is in Mbps. The 10 Gbps check below is just a sanity check.
    rt_assert(link.link_speed >= 10000, "Link too slow");
    resolve_.bandwidth_ =
        static_cast<size_t>(link.link_speed) * 1000 * 1000 / 8.0;
  } else {
    ERPC_WARN(
        "Port %u bandwidth not reported by DPDK. Using default 10 Gbps.\n",
        phy_port_);
    link.link_speed = 10000;
    resolve_.bandwidth_ = 10.0 * (1000 * 1000 * 1000) / 8.0;
  }

  ERPC_INFO(
      "Resolved port %u: MAC %s, IPv4 %s, RETA size %zu entries, bandwidth "
      "%.1f Gbps\n",
      phy_port_, mac_to_string(resolve_.mac_addr_).c_str(),
      ipv4_to_string(htonl(resolve_.ipv4_addr_)).c_str(), resolve_.reta_size_,
      resolve_.bandwidth_ * 8.0 / (1000 * 1000 * 1000));
}

/// Tokenize the input string by the delimiter into a vector
static std::vector<std::string> ipconfig_helper_split(std::string input,
                                                      char delimiter) {
  std::vector<std::string> ret;
  std::stringstream ss(input);
  std::string token;

  while (getline(ss, token, delimiter)) ret.push_back(token);
  return ret;
}

uint32_t DpdkTransport::get_port_ipv4_addr(size_t phy_port) {
  _unused(phy_port);
#ifdef _WIN32
  // Hack for now: Get the IP address of the second NIC using ipconfig.exe
  std::string ipconfig_out = "";
  {
    const std::string cmd = "ipconfig.exe | findstr.exe IPv4";
    FILE *pipe = _popen(cmd.c_str(), "r");
    rt_assert(pipe != nullptr, "Failed to open pipe to run ipconfig.exe");

    // Read from the pipe in chunks
    static constexpr size_t kChunkSize = 512;
    while (!feof(pipe)) {
      char buf[kChunkSize];
      if (fgets(buf, kChunkSize, pipe) != nullptr) ipconfig_out += buf;
    }
    _pclose(pipe);
  }

  // Here, ipconfig_out is a string of the form:
  // \"   IPv4 Address. . . . . . . . . . . : 10.0.0.8
  //      IPv4 Address. . . . . . . . . . . : 10.0.0.9\"
  rt_assert(ipconfig_out.length() > 0, "ipconfig.exe failed to report ifaces");

  // Extract the second line
  const std::vector<std::string> ip_lines =
      ipconfig_helper_split(ipconfig_out, '\n');
  rt_assert(ip_lines.size() == 2,
            "Failed to split ipconfig.exe output into two lines");

  // ip_lines[1] is the string (ex):
  // \"   IPv4 Address. . . . . . . . . . . : 10.0.0.9\"

  const std::vector<std::string> ip_addr_splits =
      ipconfig_helper_split(ip_lines[1], ':');
  rt_assert(ip_addr_splits.size() == 2,
            "Failed to split IP address line into two with delimiter :");

  // ip_addr_splits[1] is the string (ex):
  // \" 10.0.0.9\"
  rt_assert(ip_addr_splits[1].at(0) == ' ',
            "Expected space at beginning of IP address not found");
  return ipv4_from_str(ip_addr_splits[1].substr(1).c_str());
#else
  if (kIsAzure) {
    // This routine gets the IPv4 address of the interface called eth1
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq ifr;
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, "eth1", IFNAMSIZ - 1);
    int ret = ioctl(fd, SIOCGIFADDR, &ifr);
    rt_assert(ret == 0, "DPDK: Failed to get IPv4 address of eth1");
    close(fd);
    return ntohl(
        reinterpret_cast<sockaddr_in *>(&ifr.ifr_addr)->sin_addr.s_addr);
  } else {
    // As a hack, use the LSBs of the port's MAC address for IP address
    struct rte_ether_addr mac;
    rte_eth_macaddr_get(phy_port, &mac);

    uint32_t ret;
    memcpy(&ret, &mac.addr_bytes[2], sizeof(ret));
    return ret;
  }
#endif
}

void DpdkTransport::fill_local_routing_info(
    routing_info_t *routing_info) const {
  memset(static_cast<void *>(routing_info), 0, kMaxRoutingInfoSize);
  auto *ri = reinterpret_cast<eth_routing_info_t *>(routing_info);
  memcpy(ri->mac_, resolve_.mac_addr_, 6);
  ri->ipv4_addr_ = resolve_.ipv4_addr_;
  ri->udp_port_ = rx_flow_udp_port_;
  ri->rxq_id_ = qp_id_;
  ri->reta_size_ = resolve_.reta_size_;
}

// Generate most fields of the L2--L4 headers now to avoid recomputation.
bool DpdkTransport::resolve_remote_routing_info(
    routing_info_t *routing_info) const {
  auto *ri = reinterpret_cast<eth_routing_info_t *>(routing_info);

  // XXX: The header generation below will overwrite routing_info. We must
  // save/use info from routing_info before that.
  uint8_t remote_mac[6];
  memcpy(remote_mac, ri->mac_, 6);
  const uint32_t remote_ipv4_addr = ri->ipv4_addr_;
  const uint16_t remote_udp_port = ri->udp_port_;

  uint16_t i = kBaseEthUDPPort;
  for (; i < UINT16_MAX; i++) {
    union rte_thash_tuple tuple;
    tuple.v4.src_addr = resolve_.ipv4_addr_;
    tuple.v4.dst_addr = remote_ipv4_addr;
    tuple.v4.sport = i;
    tuple.v4.dport = remote_udp_port;
    uint32_t rss_l3l4 = rte_softrss(reinterpret_cast<uint32_t *>(&tuple),
                                    RTE_THASH_V4_L4_LEN, kDefaultRssKey);
    if ((rss_l3l4 % ri->reta_size_) % DpdkTransport::kMaxQueuesPerPort ==
        ri->rxq_id_)
      break;
  }
  rt_assert(i < UINT16_MAX, "Scan error");

  // Overwrite routing_info by constructing the packet header in place
  static_assert(kMaxRoutingInfoSize >= kInetHdrsTotSize, "");

  auto *eth_hdr = reinterpret_cast<eth_hdr_t *>(ri);
  gen_eth_header(eth_hdr, &resolve_.mac_addr_[0], remote_mac);

  auto *ipv4_hdr = reinterpret_cast<ipv4_hdr_t *>(&eth_hdr[1]);
  gen_ipv4_header(ipv4_hdr, resolve_.ipv4_addr_, remote_ipv4_addr, 0);

  auto *udp_hdr = reinterpret_cast<udp_hdr_t *>(&ipv4_hdr[1]);
  gen_udp_header(udp_hdr, i, remote_udp_port, 0);

  return true;
}

/// A dummy memory registration function
static Transport::mem_reg_info dpdk_reg_mr_wrapper(void *, size_t) {
  return Transport::mem_reg_info();
}

/// A dummy memory de-registration function
static void dpdk_dereg_mr_wrapper(Transport::mem_reg_info) { return; }

void DpdkTransport::init_mem_reg_funcs() {
  using namespace std::placeholders;
  reg_mr_func_ = std::bind(dpdk_reg_mr_wrapper, _1, _2);
  dereg_mr_func_ = std::bind(dpdk_dereg_mr_wrapper, _1);
}
}  // namespace erpc

#endif

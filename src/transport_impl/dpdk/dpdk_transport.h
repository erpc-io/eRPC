/**
 * @file dpdk_transport.h
 * @brief Transport for any DPDK-supported NIC
 */
#pragma once

#ifdef ERPC_DPDK

#include "transport.h"
#include "transport_impl/eth_common.h"
#include "util/barrier.h"
#include "util/logger.h"

#include <rte_common.h>
#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <signal.h>

namespace erpc {

class DpdkTransport : public Transport {
 public:
  static constexpr size_t kMaxQueuesPerPort = 16;
  static constexpr size_t kInvalidQpId = SIZE_MAX;

  /// Contents of the memzone created by the eRPC DPDK daemon process
  struct ownership_memzone_t {
   private:
    pthread_mutex_t lock_;  /// Guard for reading/writing to the memzone
    size_t epoch_;  /// Incremented after each QP ownership change attempt
    size_t num_qps_available_;

    struct {
      /// pid_ is the PID of the process that owns QP #i. Zero means
      /// the corresponding QP is free.
      int pid_;

      /// proc_random_id_ is a random number installed by the process that owns
      /// QP #i. This is used to defend against PID reuse.
      size_t proc_random_id_;
    } owner_[kMaxPhyPorts][kMaxQueuesPerPort];

   public:
    struct rte_eth_link link_[kMaxPhyPorts];  /// Resolved link status

    void init() {
      pthread_mutex_init(&lock_, nullptr);
      memset(this, 0, sizeof(ownership_memzone_t));
      num_qps_available_ = kMaxQueuesPerPort;
    }

    size_t get_epoch() {
      size_t ret;
      pthread_mutex_lock(&lock_);
      ret = epoch_;
      pthread_mutex_unlock(&lock_);
      return ret;
    }

    size_t get_num_qps_available() {
      size_t ret;
      pthread_mutex_lock(&lock_);
      ret = num_qps_available_;
      pthread_mutex_unlock(&lock_);
      return ret;
    }

    std::string get_summary(size_t phy_port) {
      pthread_mutex_lock(&lock_);
      std::ostringstream ret;
      ret << "[" << num_qps_available_ << " QPs of " << kMaxQueuesPerPort
          << " available] ";

      if (num_qps_available_ < kMaxQueuesPerPort) {
        ret << "[Ownership: ";
        for (size_t i = 0; i < kMaxQueuesPerPort; i++) {
          auto &owner = owner_[phy_port][i];
          if (owner.pid_ != 0) {
            ret << "[QP #" << i << ", "
                << "PID " << owner.pid_ << "] ";
          }
        }
        ret << "]";
      }
      pthread_mutex_unlock(&lock_);

      return ret.str();
    }

    /**
     * @brief Try to get a free QP
     *
     * @param phy_port The DPDK port ID to try getting a free QP from
     * @param proc_random_id A unique random process ID of the calling process
     *
     * @return If successful, the machine-wide global index of the free QP
     * reserved on phy_port. Else return kInvalidQpId.
     */
    size_t get_qp(size_t phy_port, size_t proc_random_id) {
      pthread_mutex_lock(&lock_);
      epoch_++;
      const int my_pid = getpid();

      // Check for sanity
      for (size_t i = 0; i < kMaxQueuesPerPort; i++) {
        auto &owner = owner_[phy_port][i];
        if (owner.pid_ == my_pid && owner.proc_random_id_ != proc_random_id) {
          ERPC_ERROR(
              "eRPC DpdkTransport: Found another process with same PID (%d) as "
              "mine. Process random IDs: mine %zu, other: %zu\n",
              my_pid, proc_random_id, owner.proc_random_id_);
          pthread_mutex_unlock(&lock_);
          return kInvalidQpId;
        }
      }

      for (size_t i = 0; i < kMaxQueuesPerPort; i++) {
        auto &owner = owner_[phy_port][i];
        if (owner.pid_ == 0) {
          owner.pid_ = my_pid;
          owner.proc_random_id_ = proc_random_id;
          num_qps_available_--;
          pthread_mutex_unlock(&lock_);
          return i;
        }
      }

      pthread_mutex_unlock(&lock_);
      return kInvalidQpId;
    }

    /**
     * @brief Try to return a QP that was previously reserved from this
     * ownership manager
     *
     * @param phy_port The DPDK port ID to try returning the QP to
     * @param qp_id The QP ID returned by this manager during reservation
     *
     * @return 0 if success, else errno
     */
    int free_qp(size_t phy_port, size_t qp_id) {
      const int my_pid = getpid();
      pthread_mutex_lock(&lock_);
      epoch_++;
      auto &owner = owner_[phy_port][qp_id];
      if (owner.pid_ == 0) {
        ERPC_ERROR("eRPC DpdkTransport: PID %d tried to already-free QP %zu.\n",
                   my_pid, qp_id);
        pthread_mutex_unlock(&lock_);
        return EALREADY;
      }

      if (owner.pid_ != my_pid) {
        ERPC_ERROR(
            "eRPC DpdkTransport: PID %d tried to free QP %zu owned by PID "
            "%d. Disallowed.\n",
            my_pid, qp_id, owner.pid_);
        pthread_mutex_unlock(&lock_);
        return EPERM;
      }

      num_qps_available_++;
      owner_[phy_port][qp_id].pid_ = 0;
      pthread_mutex_unlock(&lock_);
      return 0;
    }

    /// Free-up QPs reserved by processes that exited before freeing a QP.
    /// This is safe, but it can leak QPs because of PID reuse.
    void daemon_reclaim_qps_from_crashed(size_t phy_port) {
      pthread_mutex_lock(&lock_);

      for (size_t i = 0; i < kMaxQueuesPerPort; i++) {
        auto &owner = owner_[phy_port][i];
        if (kill(owner.pid_, 0) != 0) {
          // This means that owner.pid_ is dead
          ERPC_WARN("eRPC DPDK daemon: Reclaiming QP %zu from crashed PID %d\n",
                    i, owner.pid_);
          num_qps_available_++;
          owner_[phy_port][i].pid_ = 0;
        }
      }
      pthread_mutex_unlock(&lock_);
    }
  };

  enum class DpdkProcType { kPrimary, kSecondary };

  // Transport-specific constants
  static constexpr TransportType kTransportType = TransportType::kDPDK;
  static constexpr size_t kMTU = 1024;

  static constexpr size_t kNumTxRingDesc = 128;
  static constexpr size_t kPostlist = 32;

  // For now, this is just for erpc::Rpc to size its array of control Msgbufs
  static constexpr size_t kUnsigBatch = 32;

  /// Maximum number of packets received in rx_burst
  static constexpr size_t kRxBatchSize = 32;

  /// Number of mbufs in each mempool (one per Transport instance). The DPDK
  /// docs recommend power-of-two minus one mbufs per pool for best utilization.
  static constexpr size_t kNumMbufs = (kNumRxRingEntries * 2 - 1);

  // XXX: ixgbe does not support fast free offload, but i40e does
  static constexpr uint32_t kOffloads = DEV_TX_OFFLOAD_MULTI_SEGS;

  /// Per-element size for the packet buffer memory pool
  static constexpr size_t kMbufSize =
      (static_cast<uint32_t>(sizeof(struct rte_mbuf)) + RTE_PKTMBUF_HEADROOM +
       2048 /* For Azure, kMTU = 1024 does not work here */);

  /// Maximum data bytes (i.e., non-header) in a packet
  static constexpr size_t kMaxDataPerPkt = (kMTU - sizeof(pkthdr_t));

  static constexpr size_t kRssKeySize = 40;  /// RSS key size in bytes

  /// Key used for RSS hashing
  static constexpr uint8_t kDefaultRssKey[kRssKeySize] = {
      0x2c, 0xc6, 0x81, 0xd1, 0x5b, 0xdb, 0xf4, 0xf7, 0xfc, 0xa2,
      0x83, 0x19, 0xdb, 0x1a, 0x3e, 0x94, 0x6b, 0x9e, 0x38, 0xd9,
      0x2c, 0x9c, 0x03, 0xd1, 0xad, 0x99, 0x44, 0xa7, 0xd9, 0x56,
      0x3d, 0x59, 0x06, 0x3c, 0x25, 0xf3, 0xfc, 0x1f, 0xdc, 0x2a,
  };

  DpdkTransport(uint16_t sm_udp_port, uint8_t rpc_id, uint8_t phy_port,
                size_t numa_node, FILE *trace_file);
  void init_hugepage_structures(HugeAlloc *huge_alloc, uint8_t **rx_ring);

  ~DpdkTransport();

  void fill_local_routing_info(RoutingInfo *routing_info) const;
  bool resolve_remote_routing_info(RoutingInfo *routing_info) const;
  size_t get_bandwidth() const { return resolve_.bandwidth; }

  /// Get the mempool name to use for this port and queue pair ID
  static std::string get_mempool_name(size_t phy_port, size_t qp_id) {
    const std::string ret = std::string("erpc-mp-") + std::to_string(phy_port) +
                            std::string("-") + std::to_string(qp_id);
    rt_assert(ret.length() < RTE_MEMPOOL_NAMESIZE, "Mempool name too long");
    return ret;
  }

  /// Get the name of the memzone shared between the DPDK daemon and eRPC
  /// processes
  static std::string get_memzone_name() { return "erpc_daemon_memzone"; }

  static std::string routing_info_str(RoutingInfo *ri) {
    return reinterpret_cast<eth_routing_info_t *>(ri)->to_string();
  }

  static std::string dpdk_strerror() {
    return std::string(rte_strerror(rte_errno));
  }

  /// Convert an RX ring data pointer to its corresponding DPDK mbuf pointer
  static rte_mbuf *dpdk_dtom(uint8_t *data) {
    return reinterpret_cast<rte_mbuf *>(data - RTE_PKTMBUF_HEADROOM -
                                        sizeof(rte_mbuf));
  }

  /// Return the UDP port to use for queue \p qp_id on DPDK port \p phy_port.
  /// With DPDK, only one process is allowed to use \p phy_port, so we need not
  /// account for other processes.
  static uint16_t udp_port_for_queue(size_t phy_port, size_t qp_id) {
    return kBaseEthUDPPort + (phy_port * kMaxQueuesPerPort) + qp_id;
  }

  /// Get the IPv4 address for \p phy_port. The returned IPv4 address is assumed
  /// to be in host-byte order.
  uint32_t get_port_ipv4_addr(size_t phy_port) {
    _unused(phy_port);
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
  }

  // dpdk_transport_datapath.cc
  void tx_burst(const tx_burst_item_t *tx_burst_arr, size_t num_pkts);
  void tx_flush();
  size_t rx_burst();
  void post_recvs(size_t num_recvs);

  /// Do DPDK initialization for \p phy_port as a primary or secondary DPDK
  /// process type. \p phy_port must not have been already initialized.
  static void setup_phy_port(uint16_t phy_port, size_t numa_node,
                             DpdkProcType proc_type);

 private:
  /**
   * @brief Resolve fields in \p resolve using \p phy_port
   * @throw runtime_error if the port cannot be resolved
   */
  void resolve_phy_port();

  /// Poll for packets on this transport's RX queue until there are no more
  /// packets left
  void drain_rx_queue();

  /// Install a flow rule for queue \p qp_id on port \p phy_port. The IPv4 and
  /// UDP address are in host-byte order.
  static void install_flow_rule(size_t phy_port, size_t qp_id,
                                uint32_t ipv4_addr, uint16_t udp_port) {
    bool installed = false;

    const int ntuple_filter_supported =
        rte_eth_dev_filter_supported(phy_port, RTE_ETH_FILTER_NTUPLE);

    const int fdir_filter_supported =
        rte_eth_dev_filter_supported(phy_port, RTE_ETH_FILTER_FDIR);

    if (ntuple_filter_supported != 0 && fdir_filter_supported != 0) {
      ERPC_WARN("No flow steering supported by NIC. Apps likely won't work.\n");
      return;
    }

    // Try the simplest filter first. I couldn't get FILTER_FDIR to work with
    // ixgbe, although it technically supports flow director.
    if (ntuple_filter_supported == 0) {
      struct rte_eth_ntuple_filter ntuple;
      memset(&ntuple, 0, sizeof(ntuple));
      ntuple.flags = RTE_5TUPLE_FLAGS;
      ntuple.dst_port = rte_cpu_to_be_16(udp_port);
      ntuple.dst_port_mask = UINT16_MAX;
      ntuple.dst_ip = rte_cpu_to_be_32(ipv4_addr);
      ntuple.dst_ip_mask = UINT32_MAX;
      ntuple.proto = IPPROTO_UDP;
      ntuple.proto_mask = UINT8_MAX;
      ntuple.priority = 1;
      ntuple.queue = qp_id;

      int ret = rte_eth_dev_filter_ctrl(phy_port, RTE_ETH_FILTER_NTUPLE,
                                        RTE_ETH_FILTER_ADD, &ntuple);
      if (ret != 0) {
        ERPC_WARN("Failed to add ntuple filter. This could be survivable.\n");
      } else {
        ERPC_WARN("Installed ntuple flow rule. Queue %zu, RX UDP port = %u.\n",
                  qp_id, udp_port);
      }
      installed = (ret == 0);
    }

    if (!installed && fdir_filter_supported == 0) {
      // Use fdir filter for i40e (5-tuple not supported)
      rte_eth_fdir_filter filter;
      memset(&filter, 0, sizeof(filter));
      filter.soft_id = qp_id;
      filter.input.flow_type = RTE_ETH_FLOW_NONFRAG_IPV4_UDP;
      filter.input.flow.udp4_flow.dst_port = rte_cpu_to_be_16(udp_port);
      filter.input.flow.udp4_flow.ip.dst_ip = rte_cpu_to_be_32(ipv4_addr);
      filter.action.rx_queue = qp_id;
      filter.action.behavior = RTE_ETH_FDIR_ACCEPT;
      filter.action.report_status = RTE_ETH_FDIR_NO_REPORT_STATUS;

      int ret = rte_eth_dev_filter_ctrl(phy_port, RTE_ETH_FILTER_FDIR,
                                        RTE_ETH_FILTER_ADD, &filter);
      rt_assert(ret == 0, "Failed to add fdir flow rule: ", strerror(-1 * ret));

      ERPC_WARN("Installed flow-director rule. Queue %zu, RX UDP port = %u.\n",
                qp_id, udp_port);
    }
  }

  /// Initialize the memory registration and deregistration functions
  void init_mem_reg_funcs();

  /// For DPDK, the RX ring buffers might not always be used in a circular
  /// order. Instead, we write pointers to the Rpc's RX ring.
  uint8_t **rx_ring_;

  size_t rx_ring_head_ = 0, rx_ring_tail_ = 0;

  uint16_t rx_flow_udp_port_ = 0;  ///< The UDP port this transport listens on
  size_t qp_id_ = kInvalidQpId;    ///< The RX/TX queue pair for this Transport

  // We don't use DPDK's lcore threads, so a shared mempool with per-lcore
  // cache won't work. Instead, we use per-thread pools with zero cached mbufs.
  rte_mempool *mempool_;

  /// Info resolved from \p phy_port, must be filled by constructor.
  struct {
    uint32_t ipv4_addr;   // The port's IPv4 address in host-byte order
    uint8_t mac_addr[6];  // The port's MAC address
    size_t bandwidth;     // Link bandwidth in bytes per second
    size_t reta_size;     // Number of entries in NIC RX indirection table
  } resolve_;
};

}  // namespace erpc

#endif

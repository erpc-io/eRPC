/**
 * @file raw_transport.h
 * @brief Transport for Mellanox verbs-based raw Ethernet
 */
#ifndef ERPC_RAW_TRANSPORT_H
#define ERPC_RAW_TRANSPORT_H

#include "inet_hdrs.h"
#include "mlx5_defs.h"
#include "transport.h"
#include "transport_impl/verbs_common.h"
#include "util/barrier.h"
#include "util/logger.h"

namespace erpc {

class RawTransport : public Transport {
 public:
  ///  RPC ID i uses destination UDP port = (kBaseRawUDPPort + i)
  static constexpr uint16_t kBaseRawUDPPort = 31850;

  // Transport-specific constants
  static constexpr TransportType kTransportType = TransportType::kRaw;
  static constexpr size_t kMTU = 1024;

  // Multi-packet RQ constants
  static constexpr size_t kLogNumStrides = 9;
  static constexpr size_t kLogStrideBytes = 10;
  static constexpr size_t kStridesPerWQE = (1ull << kLogNumStrides);
  static constexpr size_t kRecvSize = (1ull << kLogStrideBytes);
  static constexpr size_t kRingSize = (kNumRxRingEntries * kRecvSize);
  static constexpr size_t kCQESnapshotCycle = 65536 * kStridesPerWQE;
  static_assert(kRecvSize >= kMTU, "");
  static_assert(kNumRxRingEntries % kStridesPerWQE == 0, "");
  static_assert(is_power_of_two(kCQESnapshotCycle), "");

  static constexpr size_t kRQDepth = (kNumRxRingEntries / kStridesPerWQE);
  static constexpr size_t kSQDepth = 128;    ///< Send queue depth
  static constexpr size_t kRecvCQDepth = 8;  // Tweakme: The overrunning CQ
  static constexpr size_t kUnsigBatch = 64;  ///< Selective signaling for SENDs
  static constexpr size_t kPostlist = 16;    ///< Maximum SEND postlist
  static constexpr size_t kMaxInline = 512;   ///< Maximum send wr inline data
  static constexpr size_t kRecvSlack = 32;   ///< RECVs batched before posting
  static_assert(is_power_of_two(kRecvCQDepth), "");
  static_assert(kSQDepth >= 2 * kUnsigBatch, "");     // Queue capacity check
  static_assert(kPostlist <= kUnsigBatch, "");        // Postlist check
  static_assert(kMaxInline >= sizeof(pkthdr_t), "");  // Inline control msgs

  /// Maximum data bytes (i.e., non-header) in a packet
  static constexpr size_t kMaxDataPerPkt = (kMTU - sizeof(pkthdr_t));

  /// Session endpoint routing info for raw Ethernet.
  struct raw_routing_info_t {
    uint8_t mac[6];
    uint32_t ipv4_addr;
    uint16_t udp_port;
  };
  static_assert(sizeof(raw_routing_info_t) <= kMaxRoutingInfoSize, "");

  /// A consistent snapshot of CQE fields in host endian format
  struct cqe_snapshot_t {
    uint16_t wqe_id;
    uint16_t wqe_counter;

    /// Return this packet's index in the CQE snapshot cycle
    size_t get_cqe_snapshot_cycle_idx() const {
      return wqe_id * kStridesPerWQE + wqe_counter;
    }

    std::string to_string() {
      std::ostringstream ret;
      ret << "[ID " << std::to_string(wqe_id) << ", counter "
          << std::to_string(wqe_counter) << "]";
      return ret.str();
    }
  };
  static_assert(sizeof(cqe_snapshot_t) == 4, "");

  /// Get a consistent snapshot of this CQE. The NIC may be concurrently DMA-ing
  /// to this CQE.
  inline void snapshot_cqe(volatile mlx5_cqe64 *cqe,
                           cqe_snapshot_t &cqe_snapshot) {
    while (true) {
      uint16_t wqe_id_0 = cqe->wqe_id;
      uint16_t wqe_counter_0 = cqe->wqe_counter;
      memory_barrier();
      uint16_t wqe_id_1 = cqe->wqe_id;

      if (likely(wqe_id_0 == wqe_id_1)) {
        cqe_snapshot.wqe_id = ntohs(wqe_id_0);
        cqe_snapshot.wqe_counter = ntohs(wqe_counter_0);
        return;
      }
    }
  }

  /// Return the CQE cycle delta between two CQE snapshots
  static inline size_t get_cqe_cycle_delta(const cqe_snapshot_t &prev,
                                           const cqe_snapshot_t &cur) {
    size_t prev_idx = prev.get_cqe_snapshot_cycle_idx();
    size_t cur_idx = cur.get_cqe_snapshot_cycle_idx();
    assert(prev_idx < kCQESnapshotCycle && cur_idx < kCQESnapshotCycle);

    return ((cur_idx + kCQESnapshotCycle) - prev_idx) % kCQESnapshotCycle;
  }

  RawTransport(uint8_t phy_port, uint8_t rpc_id);
  void init_hugepage_structures(HugeAlloc *huge_alloc, uint8_t **rx_ring);

  ~RawTransport();

  void fill_local_routing_info(RoutingInfo *routing_info) const;
  bool resolve_remote_routing_info(RoutingInfo *routing_info) const;

  static std::string routing_info_str(RoutingInfo *routing_info) {
    auto *ri = reinterpret_cast<raw_routing_info_t *>(routing_info);

    std::ostringstream ret;
    ret << "[MAC " << mac_to_string(ri->mac) << ", IP "
        << ipv4_to_string(ri->ipv4_addr) << ", UDP port "
        << std::to_string(ri->udp_port) << "]";

    return std::string(ret.str());
  }

  // ib_transport_datapath.cc
  void tx_burst(const tx_burst_item_t *tx_burst_arr, size_t num_pkts);
  void tx_flush();
  size_t rx_burst();
  void post_recvs(size_t num_recvs);

  /// Get the current SEND signaling flag, and poll the send CQ if we need to
  inline int get_signaled_flag() {
    // If kUnsigBatch is 4, the sequence of signaling and polling looks like so:
    // * nb_tx = 0: no poll, signaled
    // * nb_tx = 1: no poll, unsignaled
    // * nb_tx = 2: no poll, unsignaled
    // * nb_tx = 3: no poll, unsignaled
    // * nb_tx = 4: poll, signaled
    static_assert(is_power_of_two(kUnsigBatch), "");

    int flag;
    if ((nb_tx & (kUnsigBatch - 1)) == 0) {
      flag = IBV_SEND_SIGNALED;

      if (likely(nb_tx != 0)) {
        struct ibv_wc wc;
        while (ibv_poll_cq(send_cq, 1, &wc) == 0) {
          // Do nothing while we have no CQE or poll_cq error
        }

        if (unlikely(wc.status != 0)) {
          fprintf(stderr, "eRPC: Fatal error. Bad SEND wc status %d\n",
                  wc.status);
          exit(-1);
        }
      }
    } else {
      flag = 0;
    }

    nb_tx++;
    return flag;
  }

 private:
  /**
   * @brief Resolve fields in \p resolve using \p phy_port
   * @throw runtime_error if the port cannot be resolved
   */
  void resolve_phy_port();

  /// Initialize the memory registration and deregistration functions
  void init_mem_reg_funcs();

  /// Initialize structures that do not require eRPC hugepages: device
  /// context, protection domain, and queue pairs.
  void init_verbs_structs();
  void init_send_qp();                    ///< Iniitalize the SEND QP
  void init_recv_qp();                    ///< Initialize the RECV QP
  void install_flow_rule();               ///< Install the UDP destination flow
  void map_mlx5_overrunning_recv_cqes();  ///< Map mlx5 RECV CQEs

  /// Initialize constant fields of RECV descriptors, fill in the Rpc's
  /// RX ring, and fill the RECV queue.
  void init_recvs(uint8_t **rx_ring);
  void init_sends();  ///< Initialize constant fields of SEND work requests

  /// Info resolved from \p phy_port, must be filled by constructor.
  struct {
    int device_id = -1;  ///< Device index in list of verbs devices
    struct ibv_context *ib_ctx = nullptr;  ///< The verbs device context
    uint8_t dev_port_id = 0;  ///< 1-based port ID in device. 0 is invalid.

    std::string ibdev_name;   ///< Verbs device name (e.g., mlx5_0)
    std::string netdev_name;  ///< Verbs device name (e.g., enp4s0f0, ib0 etc)
    uint32_t ipv4_addr;       ///< The port's IPv4 address
    uint8_t mac_addr[6];      ///< The port's MAC address
  } resolve;

  struct ibv_pd *pd = nullptr;
  struct ibv_qp *send_qp = nullptr;
  struct ibv_cq *send_cq = nullptr;

  struct ibv_exp_flow *recv_flow;
  struct ibv_cq *recv_cq;
  struct ibv_exp_wq *wq;
  struct ibv_exp_wq_family *wq_family;
  struct ibv_exp_rwq_ind_table *ind_tbl;
  struct ibv_qp *recv_qp;

  Buffer ring_extent;  ///< The ring's backing hugepage memory

  // SEND
  size_t nb_tx = 0;  ///< Total number of packets sent, reset to 0 on tx_flush()
  struct ibv_send_wr send_wr[kPostlist + 1];  // +1 for unconditional ->next
  struct ibv_sge send_sgl[kPostlist][2];  ///< SGEs for eRPC header & payload

  // RECV
  size_t recvs_to_post = 0;              ///< Current number of RECVs to post
  struct ibv_sge mp_recv_sge[kRQDepth];  ///< The multi-packet RECV SGEs
  size_t mp_sge_idx = 0;  ///< Index of the multi-packet SGE to post

  // Overrunning RECV CQE
  cqe_snapshot_t prev_snapshot;
  volatile mlx5_cqe64 *recv_cqe_arr = nullptr;  ///< The overrunning RECV CQEs
  size_t cqe_idx = 0;
};

}  // End erpc

#endif  // ERPC_RAW_TRANSPORT_H

/**
 * @file raw_transport.h
 * @brief Transport for Mellanox verbs-based raw Ethernet
 */
#pragma once

#ifdef ERPC_RAW

#include "mlx5_defs.h"
#include "transport.h"
#include "transport_impl/eth_common.h"
#include "transport_impl/verbs_common.h"
#include "util/barrier.h"
#include "util/logger.h"

namespace erpc {

class RawTransport : public Transport {
 public:
  // Tweakme

  /// Enable the dumbpipe optimizations (multi-packet RECVs, overrunning CQ).
  /// This must work with the original unmodded drivers.
  static constexpr bool kDumb = true;

  /// Enable fast RECV posting (FaSST, OSDI 16). This requires the modded
  /// driver. This is irrelevant if dumbpipe optimizations are enabled.
  static constexpr bool kFastRecv = false;

  // Transport-specific constants
  static constexpr TransportType kTransportType = TransportType::kRaw;
  static constexpr size_t kMTU = 1024;

  // Multi-packet RQ constants
  static constexpr size_t kLogNumStrides = 9;
  static constexpr size_t kLogStrideBytes = 10;
  static constexpr size_t kStridesPerWQE = (1ull << kLogNumStrides);
  static constexpr size_t kCQESnapshotCycle = 65536 * kStridesPerWQE;
  static_assert((1ull << kLogStrideBytes) == kMTU, "");
  static_assert(kNumRxRingEntries % kStridesPerWQE == 0, "");
  static_assert(is_power_of_two(kCQESnapshotCycle), "");

  static constexpr size_t kRecvSize = kDumb ? (1ull << kLogStrideBytes) : kMTU;
  static constexpr size_t kRingSize = (kNumRxRingEntries * kRecvSize);
  static constexpr size_t kRQDepth =
      kDumb ? (kNumRxRingEntries / kStridesPerWQE) : kNumRxRingEntries;
  static constexpr size_t kSQDepth = 128;  ///< Send queue depth

  /// In the dumb mode, this is the CQ size allocated by the mlx5 driver. We
  /// request a CQ of half this size, and the mlx5 driver doubles it.
  /// In the non-dumb mode, this is the CQ size passed to create_cq().
  static constexpr size_t kRecvCQDepth = kDumb ? 8 : kRQDepth;
  static_assert(kRecvCQDepth >= 2 && is_power_of_two(kRecvCQDepth), "");

  static constexpr size_t kUnsigBatch = 64;  ///< Selective signaling for SENDs
  static constexpr size_t kPostlist = 16;    ///< Maximum SEND postlist
  static constexpr size_t kMaxInline = 0;    ///< Maximum send wr inline data
  static_assert(is_power_of_two(kRecvCQDepth), "");
  static_assert(kSQDepth >= 2 * kUnsigBatch, "");  // Queue capacity check
  static_assert(kPostlist <= kUnsigBatch, "");     // Postlist check

  /// Maximum data bytes (i.e., non-header) in a packet
  static constexpr size_t kMaxDataPerPkt = (kMTU - sizeof(pkthdr_t));

  /// RECVs batched before posting. Relevant only for non-dumbpipe mode.
  static constexpr size_t kRecvSlack = 32;

  /// A consistent snapshot of CQE fields in host endian format. Used only with
  /// the dumbpipe optimization.
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

  RawTransport(uint16_t sm_udp_port, uint8_t rpc_id, uint8_t phy_port,
               size_t numa_node, FILE *trace_file);
  void init_hugepage_structures(HugeAlloc *huge_alloc, uint8_t **rx_ring);

  ~RawTransport();

  void fill_local_routing_info(RoutingInfo *routing_info) const;
  bool resolve_remote_routing_info(RoutingInfo *routing_info) const;
  size_t get_bandwidth() const { return resolve.bandwidth; }

  static std::string routing_info_str(RoutingInfo *ri) {
    return reinterpret_cast<eth_routing_info_t *>(ri)->to_string();
  }

  // raw_transport_datapath.cc
  void tx_burst(const tx_burst_item_t *tx_burst_arr, size_t num_pkts);
  void tx_flush();
  size_t rx_burst();
  void post_recvs(size_t num_recvs);

  /// Get the current SEND signaling flag, and poll the send CQ if we need to
  inline bool get_signaled_flag() {
    // If kUnsigBatch is 4, the sequence of signaling and polling looks like so:
    // * nb_tx = 0: no poll, signaled
    // * nb_tx = 1: no poll, unsignaled
    // * nb_tx = 2: no poll, unsignaled
    // * nb_tx = 3: no poll, unsignaled
    // * nb_tx = 4: poll, signaled

    bool signaled;
    if (nb_tx % kUnsigBatch == 0) {
      signaled = true;
      if (likely(nb_tx != 0)) poll_cq_one_helper(send_cq);
    } else {
      signaled = false;
    }

    nb_tx++;
    return signaled;
  }

 private:
  /**
   * @brief Resolve fields specific to Raw transport in \p resolve
   * @throw runtime_error if the port cannot be resolved
   */
  void raw_resolve_phy_port();

  /// Initialize the memory registration and deregistration functions
  void init_mem_reg_funcs();

  /// Initialize structures that do not require eRPC hugepages: device
  /// context, protection domain, and queue pairs.
  void init_verbs_structs();

  /// Initialize a SEND queue, and a RECV queue if multi-packet RECVs are
  /// disabled
  void init_basic_qp();

  /// In the dumbpipe mode, initialze the multi-packet RECV QP
  void init_mp_recv_qp();
  void install_flow_rule();               ///< Install the UDP destination flow
  void map_mlx5_overrunning_recv_cqes();  ///< Map mlx5 RECV CQEs

  /// Initialize constant fields of RECV descriptors, fill in the Rpc's
  /// RX ring, and fill the RECV queue.
  void init_recvs(uint8_t **rx_ring);
  void init_sends();  ///< Initialize constant fields of SEND work requests

  /// Info resolved from \p phy_port, must be filled by constructor.
  class RawResolve : public VerbsResolve {
   public:
    std::string ibdev_name;   ///< Verbs device name (e.g., mlx5_0)
    std::string netdev_name;  ///< Verbs device name (e.g., enp4s0f0, ib0 etc)
    uint32_t ipv4_addr;       ///< The port's IPv4 address in host-byte order
    uint8_t mac_addr[6];      ///< The port's MAC address
  } resolve;

  struct ibv_pd *pd = nullptr;
  struct ibv_qp *qp = nullptr;  ///< In dumbpipe mode, used for only SENDs
  struct ibv_cq *send_cq = nullptr;
  struct ibv_cq *recv_cq = nullptr;
  struct ibv_exp_flow *recv_flow;

  // Multi-packet RQ members
  struct ibv_exp_wq *wq;
  struct ibv_exp_wq_family *wq_family;
  struct ibv_exp_rwq_ind_table *ind_tbl;
  struct ibv_qp *mp_recv_qp;

  Buffer ring_extent;  ///< The ring's backing hugepage memory

  // SEND
  size_t nb_tx = 0;  ///< Total number of packets sent, reset to 0 on tx_flush()
  struct ibv_send_wr send_wr[kPostlist + 1];  // +1 for unconditional ->next
  struct ibv_sge send_sgl[kPostlist][2];  ///< SGEs for eRPC header & payload

  // Overrunning RECV CQE
  cqe_snapshot_t prev_snapshot;
  volatile mlx5_cqe64 *recv_cqe_arr = nullptr;  ///< The overrunning RECV CQEs
  size_t cqe_idx = 0;

  // RECV
  const uint16_t rx_flow_udp_port;
  size_t recvs_to_post = 0;  ///< Current number of RECVs to post
  size_t recv_head = 0;      ///< In dumbpipe mode, used only to prefetch

  // Multi-packet RECV fields. Used only in dumbpipe mode.
  struct ibv_sge mp_recv_sge[kRQDepth];  ///< The multi-packet RECV SGEs
  size_t mp_sge_idx = 0;  ///< Index of the multi-packet SGE to post

  // Non-multi-packet RECV fields
  struct ibv_recv_wr recv_wr[kRQDepth];
  struct ibv_sge recv_sgl[kRQDepth];
  struct ibv_wc recv_wc[kRQDepth];
};

}  // namespace erpc

#endif

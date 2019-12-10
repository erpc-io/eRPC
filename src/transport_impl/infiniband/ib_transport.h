/**
 * @file ib_transport.h
 * @brief UD transport for InfiniBand or RoCE
 */
#pragma once

#ifdef ERPC_INFINIBAND

#include "transport.h"
#include "transport_impl/verbs_common.h"
#include "util/logger.h"

namespace erpc {

class IBTransport : public Transport {
 public:
  // Transport-specific constants
  static constexpr TransportType kTransportType = TransportType::kInfiniBand;

  // For IBTransport, we've kept (kRecvSize + 64) non-4096 aligned. Is this
  // ever beneficial?
  static constexpr size_t kMTU = kIsRoCE ? 1024 : 3840;

  static constexpr size_t kRecvSize = (kMTU + 64);  ///< RECV size (with GRH)
  static constexpr size_t kRQDepth = kNumRxRingEntries;  ///< RECV queue depth
  static constexpr size_t kSQDepth = 128;                ///< Send queue depth
  static constexpr size_t kUnsigBatch = 64;  ///< Selective signaling for SENDs
  static constexpr size_t kPostlist = 16;    ///< Maximum SEND postlist
  static constexpr size_t kMaxInline = 60;   ///< Maximum send wr inline data
  static constexpr size_t kRecvSlack = 32;   ///< RECVs batched before posting
  static constexpr uint32_t kQKey = 0xffffffff;  ///< Secure key for all nodes
  static constexpr size_t kGRHBytes = 40;

  static_assert(kSQDepth >= 2 * kUnsigBatch, "");  // Queue capacity check
  static_assert(kPostlist <= kUnsigBatch, "");     // Postlist check

  // Derived constants

  /// Maximum data bytes (i.e., non-header) in a packet
  static constexpr size_t kMaxDataPerPkt = (kMTU - sizeof(pkthdr_t));

  /**
   * @brief Session endpoint routing info for InfiniBand.
   *
   * The client fills in its \p port_lid and \p qpn, which are resolved into
   * the address handle by the server. Similarly for the server.
   *
   * \p port_lid, \p qpn, and \p gid have cluster-wide meaning, but \p ah is
   * local to this machine.
   *
   * The \p ibv_ah struct cannot be inlined into a RoutingInfo struct because
   * the device driver internally uses a larger struct (e.g., \p mlx4_ah for
   * ConnectX-3) which contains \p ibv_ah.
   */
  struct ib_routing_info_t {
    // Fields that are meaningful cluster-wide
    uint16_t port_lid;
    uint32_t qpn;
    union ibv_gid gid;  // RoCE only

    // Fields that are meaningful only locally
    struct ibv_ah *ah;
  };
  static_assert(sizeof(ib_routing_info_t) <= kMaxRoutingInfoSize, "");

  IBTransport(uint16_t sm_udp_port, uint8_t rpc_id, uint8_t phy_port,
              size_t numa_node, FILE *trace_file);

  void init_hugepage_structures(HugeAlloc *huge_alloc, uint8_t **rx_ring);

  ~IBTransport();

  /// Create an address handle using this routing info
  struct ibv_ah *create_ah(const ib_routing_info_t *) const;

  void fill_local_routing_info(RoutingInfo *routing_info) const;
  bool resolve_remote_routing_info(RoutingInfo *routing_info);
  size_t get_bandwidth() const { return resolve.bandwidth; }

  static std::string routing_info_str(RoutingInfo *routing_info) {
    auto *ib_routing_info = reinterpret_cast<ib_routing_info_t *>(routing_info);
    const auto &gid = ib_routing_info->gid.global;

    std::ostringstream ret;

    ret << "[LID: " << std::to_string(ib_routing_info->port_lid)
        << ", QPN: " << std::to_string(ib_routing_info->qpn)
        << ", GID interface ID " << std::to_string(gid.interface_id)
        << ", GID subnet prefix " << std::to_string(gid.subnet_prefix) << "]";

    return std::string(ret.str());
  }

  // ib_transport_datapath.cc
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
   * @brief Resolve InfiniBand-specific fields in \p resolve
   * @throw runtime_error if the port cannot be resolved
   */
  void ib_resolve_phy_port();

  /**
   * @brief Initialize structures that do not require eRPC hugepages: device
   * context, protection domain, and queue pair.
   *
   * @throw runtime_error if initialization fails
   */
  void init_verbs_structs();

  /// Initialize the memory registration and deregistration functions
  void init_mem_reg_funcs();

  /// Initialize constant fields of RECV descriptors, fill in the Rpc's
  ///  RX ring, and fill the RECV queue.
  void init_recvs(uint8_t **rx_ring);

  void init_sends();  ///< Initialize constant fields of SEND work requests

  /// Info resolved from \p phy_port, must be filled by constructor.
  class IBResolve : public VerbsResolve {
   public:
    uint16_t port_lid = 0;  ///< Port LID. 0 is invalid.
    union ibv_gid gid;      ///< GID, used only for RoCE
  } resolve;

  struct ibv_pd *pd = nullptr;
  struct ibv_cq *send_cq = nullptr, *recv_cq = nullptr;
  struct ibv_qp *qp = nullptr;

  /// An address handle for this endpoint's port. Used for tx_flush().
  struct ibv_ah *self_ah = nullptr;

  Buffer ring_extent;  ///< The ring's backing hugepage memory
  bool use_fast_recv;  ///< True iff fast RECVs are enabled

  // SEND
  size_t nb_tx = 0;  /// Total number of packets sent, reset to 0 on tx_flush()
  struct ibv_send_wr send_wr[kPostlist + 1];  /// +1 for unconditional ->next
  struct ibv_sge send_sgl[kPostlist][2];  ///< SGEs for eRPC header & payload

  // RECV
  size_t recv_head = 0;      ///< Index of current un-posted RECV buffer
  size_t recvs_to_post = 0;  ///< Current number of RECVs to post

  struct ibv_recv_wr recv_wr[kRQDepth];
  struct ibv_sge recv_sgl[kRQDepth];
  struct ibv_wc recv_wc[kRQDepth];

  // Once post_recvs_fast() is used, regular post_recv() must not be used
  bool fast_recv_used = false;

  /// Address handles that we must free in the destructor
  std::vector<ibv_ah *> ah_to_free_vec;
};

}  // namespace erpc

#endif

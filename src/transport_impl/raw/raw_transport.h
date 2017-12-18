/**
 * @file raw_transport.h
 * @brief Transport for Mellanox verbs-based raw Ethernet
 */
#ifndef ERPC_RAW_TRANSPORT_H
#define ERPC_RAW_TRANSPORT_H

#include "inet_hdrs.h"
#include "transport.h"
#include "transport_impl/verbs_common.h"
#include "util/logger.h"

namespace erpc {

class RawTransport : public Transport {
 public:
  // Transport-specific constants
  static constexpr TransportType kTransportType = TransportType::kRaw;
  static constexpr size_t kMTU = 1500;
  static constexpr size_t kRecvSize = (kMTU + 64);  ///< RECV size (with GRH)
  static constexpr size_t kUnsigBatch = 64;  ///< Selective signaling for SENDs
  static constexpr size_t kPostlist = 16;    ///< Maximum SEND postlist
  static constexpr size_t kMaxInline = 60;   ///< Maximum send wr inline data
  static constexpr size_t kRecvSlack = 32;   ///< RECVs batched before posting

  static_assert(kSendQueueDepth >= 2 * kUnsigBatch, "");  // Capacity check
  static_assert(kPostlist <= kUnsigBatch, "");            // Postlist check
  static_assert(kMaxInline >= sizeof(pkthdr_t), "");      // Inline control msgs

  // Derived constants

  /// Maximum data bytes (i.e., non-header) in a packet
  static constexpr size_t kMaxDataPerPkt = (kMTU - sizeof(pkthdr_t));

  /// Session endpoint routing info for raw Ethernet.
  struct raw_routing_info_t {
    uint8_t mac[6];
    uint32_t ip_addr;
    uint16_t udp_port;
  };
  static_assert(sizeof(raw_routing_info_t) <= kMaxRoutingInfoSize, "");

  RawTransport(uint8_t phy_port, uint8_t rpc_id);
  void init_hugepage_structures(HugeAlloc *huge_alloc, uint8_t **rx_ring);

  ~RawTransport();

  /// Create an address handle using this routing info
  struct ibv_ah *create_ah(const raw_routing_info_t *) const;

  void fill_local_routing_info(RoutingInfo *routing_info) const;
  bool resolve_remote_routing_info(RoutingInfo *routing_info) const;

  static std::string routing_info_str(RoutingInfo *routing_info) {
    auto *ri = reinterpret_cast<raw_routing_info_t *>(routing_info);

    std::ostringstream ret;
    ret << "[MAC " << mac_to_string(ri->mac) << ", IP "
        << ip_to_string(ri->ip_addr) << ", UDP port "
        << std::to_string(ri->udp_port) << "]";

    return std::string(ret.str());
  }

  static size_t data_size_to_num_pkts(size_t data_size) {
    if (data_size <= kMaxDataPerPkt) return 1;
    return (data_size + kMaxDataPerPkt - 1) / kMaxDataPerPkt;
  }

  // ib_transport_datapath.cc
  void tx_burst(const tx_burst_item_t *tx_burst_arr, size_t num_pkts);
  void tx_flush();
  void poll_send_cq_for_flush(bool first);
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

  /**
   * @brief Initialize structures that do not require eRPC hugepages: device
   * context, protection domain, and queue pair.
   *
   * @throw runtime_error if initialization fails
   */
  void init_infiniband_structs();

  /**
   * @brief A function wrapper whose \p pd argument is later bound to generate
   * this transport's \p reg_mr_func
   *
   * @throw runtime_error if memory registration fails
   */
  static MemRegInfo ibv_reg_mr_wrapper(struct ibv_pd *pd, void *buf,
                                       size_t size) {
    struct ibv_mr *mr = ibv_reg_mr(pd, buf, size, IBV_ACCESS_LOCAL_WRITE);
    rt_assert(mr != nullptr, "eRPC IBTransport: Failed to register mr.");

    LOG_INFO("eRPC IBTransport: Registered %zu MB (lkey = %u)\n", size / MB(1),
             mr->lkey);
    return MemRegInfo(mr, mr->lkey);
  }

  /**
   * @brief A function wrapper used to generate this transport's
   * \p dereg_mr_func
   */
  static void ibv_dereg_mr_wrapper(MemRegInfo mr) {
    struct ibv_mr *ib_mr = reinterpret_cast<struct ibv_mr *>(mr.transport_mr);
    size_t size = ib_mr->length;
    uint32_t lkey = ib_mr->lkey;

    int ret = ibv_dereg_mr(ib_mr);

    if (ret != 0) {
      fprintf(stderr,
              "eRPC IBTransport: Memory deregistration failed. "
              "size = %zu MB, lkey = %u.\n",
              size, lkey);
      return;
    }

    LOG_INFO("eRPC IBTransport: Deregistered %zu MB (lkey = %u)\n",
             size / MB(1), lkey);
  }

  /// Initialize the memory registration and deregistration functions
  void init_mem_reg_funcs();

  /**
   * @brief Initialize constant fields of RECV descriptors, fill in the Rpc's
   * RX ring, and fill the RECV queue.
   *
   * @throw runtime_error if RECV buffer hugepage allocation fails
   */
  void init_recvs(uint8_t **rx_ring);

  /// Initialize non-inline SEND buffers and constant fields of SEND descriptors
  void init_sends();

  static bool is_roce() { return kTransportType == TransportType::kRoCE; }
  static bool is_infiniband() {
    return kTransportType == TransportType::kInfiniBand;
  }

  // ibverbs helper functions
  static std::string link_layer_str(uint8_t link_layer) {
    switch (link_layer) {
      case IBV_LINK_LAYER_UNSPECIFIED:
        return "[Unspecified]";
      case IBV_LINK_LAYER_INFINIBAND:
        return "[InfiniBand]";
      case IBV_LINK_LAYER_ETHERNET:
        return "[Ethernet]";
      default:
        return "[Invalid]";
    }
  }

  static size_t enum_to_mtu(enum ibv_mtu mtu) {
    switch (mtu) {
      case IBV_MTU_256:
        return 256;
      case IBV_MTU_512:
        return 512;
      case IBV_MTU_1024:
        return 1024;
      case IBV_MTU_2048:
        return 2048;
      case IBV_MTU_4096:
        return 4096;
      default:
        return 0;
    }
  }

  /// InfiniBand info resolved from \p phy_port, must be filled by constructor.
  struct {
    int device_id = -1;  ///< Device index in list of verbs devices
    struct ibv_context *ib_ctx = nullptr;  ///< The verbs device context
    uint8_t dev_port_id = 0;  ///< 1-based port ID in device. 0 is invalid.
    uint16_t port_lid = 0;    ///< LID of phy_port. 0 is invalid.

    // GID for RoCE
    union ibv_gid gid;
  } resolve;

  struct ibv_pd *pd = nullptr;
  struct ibv_cq *send_cq = nullptr, *recv_cq = nullptr;
  struct ibv_qp *qp = nullptr;

  /// An address handle for this endpoint's port. Used for tx_flush().
  struct ibv_ah *self_ah = nullptr;

  Buffer recv_extent;
  bool use_fast_recv;  ///< True iff fast RECVs are enabled

  // SEND
  size_t nb_tx = 0;  /// Total number of packets sent, reset to 0 on tx_flush()
  struct ibv_send_wr send_wr[kPostlist + 1];  /// +1 for unconditional ->next
  struct ibv_sge send_sgl[kPostlist][2];      ///< SGEs for header & payload

  // RECV
  size_t recv_head = 0;      ///< Index of current un-posted RECV buffer
  size_t recvs_to_post = 0;  ///< Current number of RECVs to post

  struct ibv_recv_wr recv_wr[kRecvQueueDepth];
  struct ibv_sge recv_sgl[kRecvQueueDepth];
  struct ibv_wc recv_wc[kRecvQueueDepth];

  // Once post_recvs_fast() is used, regular post_recv() must not be used
  bool fast_recv_used = false;
};

}  // End erpc

#endif  // ERPC_RAW_TRANSPORT_H

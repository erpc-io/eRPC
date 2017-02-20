#ifndef ERPC_INFINIBAND_TRANSPORT_H
#define ERPC_INFINIBAND_TRANSPORT_H

#include <infiniband/verbs.h>
#include "transport.h"

namespace ERpc {

class IBTransport : public Transport {
  // Transport-specific constants
  static const size_t kMTU = 3840;              ///< Make (kRecvSize / 64) prime
  static const size_t kRecvSize = (kMTU + 64);  ///< RECV buf size (with GRH)
  static const size_t kUnsigBatch = 64;  ///< Selective signaling for SENDs
  static const size_t kPostlist = 16;    ///< Maximum postlist size for SENDs
  static const size_t kMaxInline = 60;   ///< Maximum send wr inline data
  static const size_t kRecvSlack = 32;   ///< RECVs accumulated before posting
  static const uint32_t kQKey = 0xffffffff;  ///< Secure key for all eRPC nodes

  static_assert(kSendQueueDepth >= 2 * kUnsigBatch, ""); /* Capacity check */
  static_assert(kPostlist <= kUnsigBatch, "");           /* Postlist check */

  struct ib_routing_info_t {
    uint16_t port_lid;
    uint32_t qpn;
  };
  static_assert(sizeof(ib_routing_info_t) <= kMaxRoutingInfoSize, "");

 public:
  /**
   * @brief Partially construct the transport object without using eRPC's
   * hugepage allocator. The device driver is allowed to use its own hugepages.
   *
   * This function must initialize \p reg_mr_func and \p dereg_mr_func.
   *
   * @throw \p runtime_error if creation fails.
   */
  IBTransport(uint8_t phy_port, uint8_t app_tid);

  /**
   * @brief Finish transport initialization using \p huge_alloc
   *
   * @throw \p runtime_error if initialization fails.
   */
  void init_hugepage_structures(HugeAllocator *huge_alloc);

  ~IBTransport();

  void fill_routing_info(RoutingInfo *routing_info) const;

  static std::string routing_info_str(RoutingInfo *routing_info) {
    ib_routing_info_t *ib_routing_info = (ib_routing_info_t *)routing_info;
    std::ostringstream ret;
    ret << "[LID: " << std::to_string(ib_routing_info->port_lid)
        << ", QPN: " << std::to_string(ib_routing_info->qpn) << "]";
    return std::string(ret.str());
  }

  // ib_transport_datapath.cc
  void send_message(Session *session, const Buffer *buffer);
  void poll_completions();

 private:
  /**
   * @brief Fill in \p ib_ctx, \p device_id, and \p dev_port using \p phy_port.
   * @throw \p runtime_error if the port cannot be resolved.
   */
  void resolve_phy_port();

  /**
   * @brief Initialize structures that do not require eRPC hugepages: device
   * context, protection domain, and queue pair.
   *
   * @throw \p runtime_error if initialization fails.
   */
  void init_infiniband_structs();

  /**
   * @brief A function wrapper whose \p pd argument is later bound to generate
   * this transport's \p reg_mr_func
   *
   * @throw \p runtime_error if memory registration fails.
   */
  static MemRegInfo ibv_reg_mr_wrapper(struct ibv_pd *pd, void *buf,
                                       size_t size) {
    struct ibv_mr *mr = ibv_reg_mr(pd, buf, size, IBV_ACCESS_LOCAL_WRITE);
    if (mr == nullptr) {
      throw std::runtime_error(
          "eRPC IBTransport: Failed to register memory region");
    }

    erpc_dprintf("eRPC IBTransport: Registered %zu MB (lkey = %u)\n",
                 size / MB(1), mr->lkey);
    return MemRegInfo(mr, mr->lkey);
  }

  /**
   * @brief A function wrapper used to generate this transport's
   * \p dereg_mr_func
   */
  static void ibv_dereg_mr_wrapper(MemRegInfo mr) {
    struct ibv_mr *ib_mr = (struct ibv_mr *)mr.transport_mr;
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

    erpc_dprintf("eRPC IBTransport: Deregistered %zu MB (lkey = %u)\n",
                 size / MB(1), lkey);
  }

  /// Initialize the memory registration and deregistration functions
  void init_mem_reg_funcs();

  /**
   * @brief Initialize RECV buffers and constant fields of RECV descriptors
   * @throw \p runtime_error if RECV buffer hugepage allocation fails
   */
  void init_recvs();

  /// Initialize non-inline SEND buffers and constant fields of SEND descriptors
  void init_sends();

  // InfiniBand info
  struct ibv_context *ib_ctx = nullptr;
  int device_id = -1;    /* Resolved from \p phy_port */
  int dev_port_id = -1;  /* 1-based, unlike \p phy_port */
  uint16_t port_lid = 0; /* InfiniBand LID of \p phy_port. 0 is invalid. */
  struct ibv_pd *pd = nullptr;
  struct ibv_cq *send_cq = nullptr, *recv_cq = nullptr;
  struct ibv_qp *qp = nullptr;
  Buffer recv_extent, req_retrans_extent;

  // SEND
  size_t nb_pending = 0;                     /* For selective signalling */
  struct ibv_send_wr send_wr[kPostlist + 1]; /* +1 for blind ->next */
  struct ibv_sge send_sgl[kPostlist];        /* No need for +1 here */

  // RECV
  size_t recv_head = 0;     /* Current un-posted RECV buffer */
  size_t recv_slack = 0;    /* RECVs to accumulate before post_recv() */
  size_t recvs_to_post = 0; /* Current number of RECVs to post */

  struct ibv_recv_wr recv_wr[kRecvQueueDepth];
  struct ibv_sge recv_sgl[kRecvQueueDepth];
  struct ibv_wc wc[kRecvQueueDepth];

  /* Once post_recvs_fast() is used, regular post_recv() must not be used */
  bool fast_recv_used = false;
};

}  // End ERpc

#endif  // ERPC_INFINIBAND_TRANSPORT_H

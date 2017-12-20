/**
 * @file verbs_common.h
 * @brief Common definitions for ibverbs-based transports
 */
#ifndef ERPC_VERBS_COMMON_H
#define ERPC_VERBS_COMMON_H

#include <infiniband/verbs.h>
#include <string>
#include "transport.h"
#include "util/logger.h"

namespace erpc {
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

/**
 * @brief A function wrapper whose \p pd argument is later bound to generate
 * this transport's \p reg_mr_func
 *
 * @throw runtime_error if memory registration fails
 */
static Transport::MemRegInfo ibv_reg_mr_wrapper(struct ibv_pd *pd, void *buf,
                                                size_t size) {
  struct ibv_mr *mr = ibv_reg_mr(pd, buf, size, IBV_ACCESS_LOCAL_WRITE);
  rt_assert(mr != nullptr, "eRPC Verbs: Failed to register mr.");

  LOG_INFO("eRPC Verbs: Registered %zu MB (lkey = %u)\n", size / MB(1),
           mr->lkey);
  return Transport::MemRegInfo(mr, mr->lkey);
}

/**
 * @brief A function wrapper used to generate this transport's
 * \p dereg_mr_func
 */
static void ibv_dereg_mr_wrapper(Transport::MemRegInfo mr) {
  struct ibv_mr *ib_mr = reinterpret_cast<struct ibv_mr *>(mr.transport_mr);
  size_t size = ib_mr->length;
  uint32_t lkey = ib_mr->lkey;

  int ret = ibv_dereg_mr(ib_mr);

  if (ret != 0) {
    LOG_ERROR("eRPC Verbs: Memory degistration failed. size %zu B, lkey %u\n",
              size / MB(1), lkey);
  }

  LOG_INFO("eRPC Verbs: Deregistered %zu B, lkey = %u\n", size, lkey);
}

// This is a slower polling function than the one used in datapaths: it prints
// a warning message when the number of polling attempts gets too high. This
// overhead is fine because the send queue is flushed rarely.
static void poll_send_cq_for_flush(struct ibv_cq *send_cq, bool first) {
  struct ibv_wc wc;
  size_t num_tries = 0;
  while (ibv_poll_cq(send_cq, 1, &wc) == 0) {
    num_tries++;
    if (num_tries == 1000000000) {
      fprintf(stderr,
              "eRPC: Warning. tx_flush stuck polling for %s signaled wr.\n",
              first ? "first" : "second");
      num_tries = 0;
    }
  }

  if (unlikely(wc.status != 0)) {
    fprintf(stderr, "eRPC: Fatal error. Bad SEND wc status %d\n", wc.status);
    exit(-1);
  }
}

}  // End erpc

#endif  // ERPC_VERBS_COMMON_H

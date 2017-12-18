/**
 * @file verbs_common.h
 * @brief Common definitions for ibverbs-based transports
 */
#ifndef ERPC_VERBS_COMMON_H
#define ERPC_VERBS_COMMON_H

#include <infiniband/verbs.h>
#include <string>

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
}  // End erpc

#endif  // ERPC_VERBS_COMMON_H

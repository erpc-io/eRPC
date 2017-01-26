/**
 * @file transport_types.h
 * @brief The fabrics supported by eRPC. This stuff cannot go in transport.h
 * because that will create a circular dependency between transport.h and
 * session.h. (transport.h requires session.h, but session.h requires only
 * TransportType nad RoutingInfo.)
 */

#ifndef ERPC_TRANSPORT_TYPE_H
#define ERPC_TRANSPORT_TYPE_H

#include <strings.h>
#include <string>
#include "common.h"

namespace ERpc {

static const size_t kRecvQueueSize = 2048; ///< RECV queue size
static const size_t kSendQueueSize = 64; ///< SEND queue size
static const size_t kSessionCredits = 8; ///< Credits per session endpoint
static const size_t kRpcWindowSize = 20; ///< Max outstanding pkts per Rpc
static const size_t kMaxRoutingInfoSize = 128; ///< Space for routing info

static_assert(is_power_of_two<size_t>(kRecvQueueSize), "");
static_assert(is_power_of_two<size_t>(kSendQueueSize), "");

enum class TransportType { kInfiniBand, kRoCE, kOmniPath, kInvalidTransport };

/**
 * @brief Generic class to store routing info for any transport.
 */
struct RoutingInfo {
  uint8_t buf[kMaxRoutingInfoSize];
};

static std::string get_transport_name(TransportType transport_type) {
  switch (transport_type) {
    case TransportType::kInfiniBand:
      return std::string("[InfiniBand]");
    case TransportType::kRoCE:
      return std::string("[RoCE]");
    case TransportType::kOmniPath:
      return std::string("[OmniPath]");
    default:
      return std::string("[Invalid transport]");
  }
}
}  // End ERpc

#endif  // ERPC_TRANSPORT_TYPE_H

#ifndef ERPC_TRANSPORT_TYPE_H
#define ERPC_TRANSPORT_TYPE_H

#include <strings.h>
#include <string>
#include "common.h"

namespace ERpc {

enum class TransportType { kInfiniBand, kRoCE, kOmniPath, kInvalidTransport };

/**
 * @brief Generic class to store routing info for any transport.
 */
static const size_t kMaxRoutingInfoSize = 128;
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

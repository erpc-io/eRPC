#ifndef ERPC_TRANSPORT_TYPE_H
#define ERPC_TRANSPORT_TYPE_H

#include <strings.h>
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

/**
 * @brief Returns an enum representation of the transport type string
 */
static TransportType get_transport_type(const char *transport_name) {
  if (strcasecmp(transport_name, "InfiniBand")) {
    return TransportType::kInfiniBand;
  } else if (strcasecmp(transport_name, "RoCE")) {
    return TransportType::kRoCE;
  } else if (strcasecmp(transport_name, "OmniPath")) {
    return TransportType::kOmniPath;
  } else {
    return TransportType::kInvalidTransport;
  }
}

}  // End ERpc

#endif  // ERPC_TRANSPORT_TYPE_H

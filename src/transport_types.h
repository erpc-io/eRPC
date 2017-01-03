#ifndef ERPC_TRANSPORT_TYPE_H
#define ERPC_TRANSPORT_TYPE_H

#include <strings.h>

namespace ERpc {

enum TransportType { InfiniBand, RoCE, OmniPath, Invalid };

/**
 * @brief Returns an enum representation of the transport type string
 */
TransportType get_transport_type(const char *transport_name) {
  if (strcasecmp(transport_name, "InfiniBand")) {
    return TransportType::InfiniBand;
  } else if (strcasecmp(transport_name, "RoCE")) {
    return TransportType::RoCE;
  } else if (strcasecmp(transport_name, "OmniPath")) {
    return TransportType::OmniPath;
  } else {
    return TransportType::Invalid;
  }
}

}  // End ERpc

#endif  // ERPC_TRANSPORT_TYPE_H

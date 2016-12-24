#ifndef ERPC_TRANSPORT_TYPE_H
#define ERPC_TRANSPORT_TYPE_H

#include <strings.h>

namespace ERpc {

enum TransportType { InfiniBand, RoCE, OmniPath, Invalid };

/**
 * @brief Returns an enum representation of the transport type string
 */
TransportType getTransportType(const char *TransportType) {
  if (strcasecmp(TransportType, "InfiniBand")) {
    return TransportType::InfiniBand;
  } else if (strcasecmp(TransportType, "RoCE")) {
    return TransportType::RoCE;
  } else if (strcasecmp(TransportType, "OmniPath")) {
    return TransportType::OmniPath;
  } else {
    return TransportType::Invalid;
  }
}

} // End ERpc

#endif // ERPC_TRANSPORT_TYPE_H

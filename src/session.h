#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include <string>

#include "common.h"
#include "transport_types.h"

namespace ERpc {

// A one-to-one session class for all transports
class Session {
 public:
  Session(const char *transport_name, const char *_hostname, int rem_port_index)
      : rem_port_index(rem_port_index) {
    transport_type = get_transport_type(transport_name);
    if (transport_type == TransportType::Invalid) {
      fprintf(stderr, "ERpc: Invalid transport type %s\n", transport_name);
    }

    hostname = std::string(_hostname);
  }

  ~Session() {}

  /**
   * @brief Enables congestion control for this session
   */
  void enable_congestion_control() { is_cc = true; }

  /**
   * @brief Disables congestion control for this session
   */
  void disable_congestion_control() { is_cc = false; }

  // The information required to resolve the remote port
  TransportType transport_type;
  std::string hostname;
  int rem_port_index;  // 0-based index in the device list of the remote port

  bool is_cc;  // Is congestion control enabled for this session?

  // InfiniBand UD. XXX: Can we reuse these fields?
  struct ibv_ah *rem_ah;
  int rem_qpn;
};

}  // End ERpc

#endif  // ERPC_SESSION_H

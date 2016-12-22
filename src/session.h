#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include "common.h"
#include "transport.h"

namespace ERpc {

// A one-to-one session class for all transports
class Session {
public:
  Session(const char* transport_type,
      const char* hostname, int rem_port_index) {


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
  std::string transport_type;
  std::string hostname;
  int rem_port_index; // 0-based index in the device list of the remote port


  bool is_cc; // Is congestion control enabled for this session?

  // InfiniBand UD. XXX: Can we reuse these fields?
  struct ibv_ah *rem_ah;
  int rem_qpn;
};

} // End ERpc

#endif // ERPC_SESSION_H

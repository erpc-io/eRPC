#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include "common.h"

namespace ERpc {

// A one-to-one session class for all transports
class Session {
public:
  Session(std::string remote_port_name) : remote_port_name(remote_port_name) {
    is_cc = false;
  }

  ~Session() {}

  /**
   * @brief Enables congestion control for this session
   */
  void enable_congestion_control() {
    is_cc = true;
  }

  /**
   * @brief Disables congestion control for this session
   */
  void disable_congestion_control() {
    is_cc = false;
  }

  // Name of the remote port that this session is connected to
  std::string remote_port_name;

  // Is congestion control enabled for this session?
  bool is_cc;

  /*
   * Transport-specific session information
   */

  // InfiniBand UD. XXX: Can we reuse these fields?
  struct ibv_ah *rem_ah;
  int rem_qpn;
};

} // End ERpc

#endif //ERPC_SESSION_H

#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include <string>

#include "common.h"
#include "transport_types.h"

namespace ERpc {

class SessionEstablishmentReq {
  TransportType transport_type;
  int client_sn;            /* Session number at client */
  size_t client_start_seq;  /* Starting sequence number chosen by client */
  RoutingInfo client_route; /* Transport-specific routing info of client */
};

class SessionEstablishmentResp {
  int server_sn;            /* Session number at server */
  size_t server_start_seq;  /* Starting sequence number chosen by server */
  RoutingInfo server_route; /* Transport-specific routing info of server */
};

// A one-to-one session class for all transports
class Session {
 public:
  Session(const char *transport_name, const char *_hostname, int rem_port_index);
  ~Session();

  /**
   * @brief Enables congestion control for this session
   */
  void enable_congestion_control();

  /**
   * @brief Disables congestion control for this session
   */
  void disable_congestion_control();

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

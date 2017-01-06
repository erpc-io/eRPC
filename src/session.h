#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include <mutex>
#include <queue>
#include <string>

#include "common.h"
#include "transport_types.h"

namespace ERpc {

class SessionEstablishmentReq {
 public:
  TransportType transport_type;
  int client_sn;            /* Session number at client */
  size_t client_start_seq;  /* Starting sequence number chosen by client */
  RoutingInfo client_route; /* Transport-specific routing info of client */
};

class SessionEstablishmentResp {
 public:
  int server_sn;            /* Session number at server */
  size_t server_start_seq;  /* Starting sequence number chosen by server */
  RoutingInfo server_route; /* Transport-specific routing info of server */
};

/**
 * @brief An object shared between the per-thread Rpc and the per-process Nexus.
 * All accesses must be done with @session_mgmt_mutex locked.
 */
class SessionManagementHook {
 public:
  std::mutex session_mgmt_mutex;
  size_t session_mgmt_req_counter;
  std::queue<SessionEstablishmentReq> session_req_queue;
  std::queue<SessionEstablishmentResp> session_resp_queue;

  SessionManagementHook() : session_mgmt_req_counter(0) {}
};

/**
 * @brief A one-to-one session class for all transports
 */
class Session {
 public:
  Session(const char *transport_name, const char *_hostname,
          int rem_port_index);
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

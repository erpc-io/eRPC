#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include <mutex>
#include <queue>
#include <string>

#include "common.h"
#include "transport_types.h"

namespace ERpc {

enum SessionMgmtEventType { kConnected, kDisconnected };

enum SessionMgmtPktType {
  kConnectReq,
  kConnectResp,
  kDisconnectReq,
  kDisconnectResp
};

/**
 * @brief General-purpose session management packet sent by both Rpc clients
 * and servers. This is pretty large (~500 bytes), so use sparingly.
 */
class SessionMgmtPkt {
  SessionMgmtPktType pkt_type;

  /*
   * Each session management packet contains two copies of this struct, filled
   * in by the client and server Rpc.
   */
  struct {
    TransportType transport_type; /* Should match at client and server */
    char hostname[kMaxHostnameLen];
    int app_tid; /* App-level TID of the Rpc object */
    int fdev_port_index;
    int session_num;
    size_t start_seq;
    RoutingInfo routing_info;
  } client, server;
};

/**
 * @brief An object created by the per-thread Rpc, and shared with the
 * per-process Nexus. All accesses must be done with @session_mgmt_mutex locked.
 */
class SessionMgmtHook {
 public:
  int app_tid; /* App-level thread ID of the RPC obj that created this hook */
  std::mutex session_mgmt_mutex;
  volatile size_t session_mgmt_ev_counter; /* Number of session mgmt events */
  std::vector<SessionMgmtPkt *> session_mgmt_pkt_list;

  SessionMgmtHook() : session_mgmt_ev_counter(0) {}
};

/**
 * @brief A one-to-one session class for all transports
 */
class Session {
 public:
  Session(int session_num, const char *_rem_hostname, int rem_fdev_port_index);
  ~Session();

  /**
   * @brief Enables congestion control for this session
   */
  void enable_congestion_control();

  /**
   * @brief Disables congestion control for this session
   */
  void disable_congestion_control();

  // Local information
  int local_fdev_port_index;
  int session_num; /* The Rpc object assigns a unique session number */

  /* Information about the remote session */
  std::string rem_hostname;
  int rem_app_tid;
  int rem_fdev_port_index; /* 0-based port index in the remote device list */

  bool is_cc; /* Is congestion control enabled for this session? */

  /* InfiniBand UD. XXX: Can we reuse these fields? */
  struct ibv_ah *rem_ah;
  int rem_qpn;
};

typedef void (*session_mgmt_handler_t)(Session *, SessionMgmtEventType, void *);

}  // End ERpc

#endif  // ERPC_SESSION_H

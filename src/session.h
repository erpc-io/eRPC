#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include <mutex>
#include <queue>
#include <string>

#include "common.h"
#include "transport_types.h"

namespace ERpc {

enum class SessionStatus {
  kInit,
  kConnectInProgress,
  kConnected,
  kDisconnected
};

/**
 * @brief Events generated for application-level session management handler
 */
enum class SessionMgmtEventType { kConnected, kDisconnected };

static std::string session_mgmt_event_type_str(
    SessionMgmtEventType event_type) {
  switch (event_type) {
    case SessionMgmtEventType::kConnected:
      return std::string("[Connected]");
    case SessionMgmtEventType::kDisconnected:
      return std::string("[Disconnected]");
  }
  return std::string("[Invalid]");
}

/**
 * @brief Types of packets used for session management
 */
enum class SessionMgmtPktType : int {
  kConnectReq,
  kConnectResp,
  kDisconnectReq,
  kDisconnectResp
};

static std::string session_mgmt_pkt_type_str(SessionMgmtPktType sm_pkt_type) {
  switch (sm_pkt_type) {
    case SessionMgmtPktType::kConnectReq:
      return std::string("[Connect request]");
    case SessionMgmtPktType::kConnectResp:
      return std::string("[Connect response]");
    case SessionMgmtPktType::kDisconnectReq:
      return std::string("[Disconnect request]");
    case SessionMgmtPktType::kDisconnectResp:
      return std::string("[Disconnect response]");
  };
  return std::string("[Invalid type]");
}

/**
 * @brief Check if a session management packet type is valid
 */
static bool is_valid_session_mgmt_pkt_type(SessionMgmtPktType sm_pkt_type) {
  switch (sm_pkt_type) {
    case SessionMgmtPktType::kConnectReq:
    case SessionMgmtPktType::kConnectResp:
    case SessionMgmtPktType::kDisconnectReq:
    case SessionMgmtPktType::kDisconnectResp:
      return true;
  }
  return false;
}

/**
 * @brief Check if a valid session management packet type is a request type. Use
 * the complement of this to check if a packet is a response.
 */
static bool is_session_mgmt_pkt_type_req(SessionMgmtPktType sm_pkt_type) {
  switch (sm_pkt_type) {
    case SessionMgmtPktType::kConnectReq:
    case SessionMgmtPktType::kDisconnectReq:
      return true;
    case SessionMgmtPktType::kConnectResp:
    case SessionMgmtPktType::kDisconnectResp:
      return false;
  }

  assert(false);
}

/**
 * @brief Basic info about a session filled in during initialization.
 */
class SessionMetadata {
 public:
  TransportType transport_type; /* Should match at client and server */
  char hostname[kMaxHostnameLen];
  int app_tid; /* App-level TID of the Rpc object */
  int fdev_port_index;
  int session_num;
  size_t start_seq;
  RoutingInfo routing_info;

  /* Fill invalid metadata to aid debugging */
  SessionMetadata() {
    transport_type = TransportType::kInvalidTransport;
    memset((void *)hostname, 0, sizeof(hostname));
    app_tid = -1;
    fdev_port_index = -1;
    session_num = -1;
    start_seq = 0;
    memset((void *)&routing_info, 0, sizeof(routing_info));
  }
};

/**
 * @brief General-purpose session management packet sent by both Rpc clients
 * and servers. This is pretty large (~500 bytes), so use sparingly.
 */
class SessionMgmtPkt {
 public:
  SessionMgmtPktType pkt_type;

  /*
   * Each session management packet contains two copies of session metadata,
   * filled in by the client and server Rpc.
   */
  SessionMetadata client, server;

  SessionMgmtPkt() {}
  SessionMgmtPkt(SessionMgmtPktType pkt_type) : pkt_type(pkt_type) {}
};
static_assert(sizeof(SessionMgmtPkt) < 1400,
              "Session management packet too large for UDP");

/**
 * @brief A one-to-one session class for all transports
 */
class Session {
 public:
  Session();
  ~Session();

  std::string get_client_name();

  /**
   * @brief Enables congestion control for this session
   */
  void enable_congestion_control();

  /**
   * @brief Disables congestion control for this session
   */
  void disable_congestion_control();

  SessionMetadata client, server;

  bool is_cc; /* Is congestion control enabled for this session? */
};

typedef void (*session_mgmt_handler_t)(Session *, SessionMgmtEventType, void *);

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

}  // End ERpc

#endif  // ERPC_SESSION_H

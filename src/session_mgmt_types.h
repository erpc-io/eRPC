#ifndef ERPC_SESSION_MGMT_PKT_TYPE_H
#define ERPC_SESSION_MGMT_PKT_TYPE_H

#include <mutex>
#include <sstream>
#include <string>
#include "common.h"
#include "transport_types.h"
#include "util/udp_client.h"

namespace ERpc {

/*
 * Maximum number of sessions (both as client and server) that can be created
 * by an Rpc through its lifetime. Increase this for more sessions.
 */
static const size_t kMaxSessionsPerThread = 1024;
static_assert(kMaxSessionsPerThread < std::numeric_limits<uint16_t>::max(), "");

static const size_t kSecretBits = 32;  ///< Session secret for security
static_assert(kSecretBits <= 32, "");  /* Secret must fit in 32 bits */

static const size_t kSessionMgmtRetransMs = 5;  ///< Timeout for management reqs
static const size_t kSessionMgmtTimeoutMs = 50;  ///< Max time for mgmt reqs

/* Invalid metadata values for session endpoint initialization */
static const uint8_t kInvalidPhyPort = std::numeric_limits<uint8_t>::max();
static const uint8_t kInvalidAppTid = std::numeric_limits<uint8_t>::max();
static const uint16_t kInvalidSessionNum = std::numeric_limits<uint16_t>::max();
static const uint32_t kInvalidSecret = 0;

/// Session state that can only go forward.
enum class SessionState {
  kConnectInProgress,
  kConnected,  ///< The only state for server-side sessions
  kDisconnectInProgress,
  kDisconnected,  ///< Temporary state just for the disconnected callback
  kError          /// Only allowed for client-side sessions
};

/// High-level types of packets used for session management
enum class SessionMgmtPktType : int {
  kConnectReq,
  kConnectResp,
  kDisconnectReq,
  kDisconnectResp
};

/// The types of responses to a session management packet
enum class SessionMgmtErrType : int {
  kNoError,         /* The only non-error error type */
  kTooManySessions, /* Connect req failed because server is out of sessions */
  kInvalidRemoteAppTid,
  kInvalidRemotePort,
  kInvalidTransport
};

/// Events generated for application-level session management handler
enum class SessionMgmtEventType {
  kConnected,
  kConnectFailed,
  kDisconnected,
  kDisconnectFailed
};

static std::string session_state_str(SessionState state) {
  switch (state) {
    case SessionState::kConnectInProgress:
      return std::string("[Connect in progress]");
    case SessionState::kConnected:
      return std::string("[Connected]");
    case SessionState::kDisconnectInProgress:
      return std::string("[Disconnect in progress]");
    case SessionState::kDisconnected:
      return std::string("[Disconnected]");
    case SessionState::kError:
      return std::string("[Error]");
  }
  return std::string("[Invalid state]");
}

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
  exit(-1);
  return std::string("");
}

/// Check if a session management packet type is valid
static bool session_mgmt_pkt_type_is_valid(SessionMgmtPktType sm_pkt_type) {
  switch (sm_pkt_type) {
    case SessionMgmtPktType::kConnectReq:
    case SessionMgmtPktType::kConnectResp:
    case SessionMgmtPktType::kDisconnectReq:
    case SessionMgmtPktType::kDisconnectResp:
      return true;
  }
  return false;
}

/// Check if a valid session management packet type is a request type. Use
/// the complement of this to check if a packet is a response.
static bool session_mgmt_pkt_type_is_req(SessionMgmtPktType sm_pkt_type) {
  assert(session_mgmt_pkt_type_is_valid(sm_pkt_type));

  switch (sm_pkt_type) {
    case SessionMgmtPktType::kConnectReq:
    case SessionMgmtPktType::kDisconnectReq:
      return true;
    case SessionMgmtPktType::kConnectResp:
    case SessionMgmtPktType::kDisconnectResp:
      return false;
  }
  exit(-1);
  return false;
}

/// Convert the request session management packet type sm_pkt_type to its
/// corresponding response packet type.
static SessionMgmtPktType session_mgmt_pkt_type_req_to_resp(
    SessionMgmtPktType sm_pkt_type) {
  assert(session_mgmt_pkt_type_is_req(sm_pkt_type));

  switch (sm_pkt_type) {
    case SessionMgmtPktType::kConnectReq:
      return SessionMgmtPktType::kConnectResp;
    case SessionMgmtPktType::kDisconnectReq:
      return SessionMgmtPktType::kDisconnectResp;
    case SessionMgmtPktType::kConnectResp:
    case SessionMgmtPktType::kDisconnectResp:
      break;
  }

  exit(-1);
  return static_cast<SessionMgmtPktType>(-1);
}

static bool session_mgmt_err_type_is_valid(SessionMgmtErrType err_type) {
  switch (err_type) {
    case SessionMgmtErrType::kNoError:
    case SessionMgmtErrType::kTooManySessions:
    case SessionMgmtErrType::kInvalidRemoteAppTid:
    case SessionMgmtErrType::kInvalidRemotePort:
    case SessionMgmtErrType::kInvalidTransport:
      return true;
  }
  return false;
}

static std::string session_mgmt_err_type_str(SessionMgmtErrType err_type) {
  assert(session_mgmt_err_type_is_valid(err_type));

  switch (err_type) {
    case SessionMgmtErrType::kNoError:
      return std::string("[No error]");
    case SessionMgmtErrType::kTooManySessions:
      return std::string("[Too many sessions]");
    case SessionMgmtErrType::kInvalidRemoteAppTid:
      return std::string("[Invalid remote app TID]");
    case SessionMgmtErrType::kInvalidRemotePort:
      return std::string("[Invalid remote port]");
    case SessionMgmtErrType::kInvalidTransport:
      return std::string("[Invalid transport]");
  }
  exit(-1);
  return std::string("");
}

static std::string session_mgmt_event_type_str(
    SessionMgmtEventType event_type) {
  switch (event_type) {
    case SessionMgmtEventType::kConnected:
      return std::string("[Connected]");
    case SessionMgmtEventType::kConnectFailed:
      return std::string("[Connect failed]");
    case SessionMgmtEventType::kDisconnected:
      return std::string("[Disconnected]");
    case SessionMgmtEventType::kDisconnectFailed:
      return std::string("[kDisconnect failed]");
  }
  return std::string("[Invalid event type]");
}

/// Basic metadata about a session end point filled when the session is created
class SessionEndpoint {
 public:
  TransportType transport_type;
  char hostname[kMaxHostnameLen];  ///< Hostname of this endpoint
  uint8_t phy_port;                ///< Fabric port used by this endpoint
  uint8_t app_tid;       ///< TID of the Rpc that created this endpoint
  uint16_t session_num;  ///< The session number of this endpoint in its Rpc
  uint32_t secret : kSecretBits;  ///< Secret for both session endpoints
  RoutingInfo routing_info;       ///< Generic routing info for this endpoint

  /* Fill invalid metadata to aid debugging */
  SessionEndpoint() {
    transport_type = TransportType::kInvalidTransport;
    memset((void *)hostname, 0, sizeof(hostname));
    phy_port = kInvalidPhyPort;
    app_tid = kInvalidAppTid;
    session_num = kInvalidSessionNum;
    secret = kInvalidSecret;
    memset((void *)&routing_info, 0, sizeof(routing_info));
  }

  /// Return a string with a name for this session endpoint, containing
  /// its hostname, Rpc TID, and the session number.
  inline std::string name() {
    std::ostringstream ret;
    std::string session_num_str = (session_num == kInvalidSessionNum)
                                      ? "XX"
                                      : std::to_string(session_num);

    ret << "[H: " << trim_hostname(hostname)
        << ", R: " << std::to_string(app_tid) << ", S: " << session_num_str
        << "]";
    return ret.str();
  }

  /// Return a string with the name of the Rpc hosting this session endpoint.
  inline std::string rpc_name() {
    std::ostringstream ret;
    ret << "[H: " << trim_hostname(hostname)
        << ", R: " << std::to_string(app_tid) << "]";
    return ret.str();
  }

  /// Compare the location fields of two SessionEndpoint objects. This does not
  /// account for non-location fields (e.g., fabric port, routing info).
  bool operator==(const SessionEndpoint &other) {
    return strcmp(hostname, other.hostname) == 0 && app_tid == other.app_tid &&
           session_num == other.session_num;
  }
};

/// General-purpose session management packet sent by both Rpc clients and
/// servers. This is pretty large (~500 bytes), so use sparingly.
class SessionMgmtPkt {
 public:
  SessionMgmtPktType pkt_type;
  SessionMgmtErrType err_type; /* For responses only */

  SessionEndpoint client, server; /* Filled in by client and server Rpc */

  SessionMgmtPkt() {}
  SessionMgmtPkt(SessionMgmtPktType pkt_type) : pkt_type(pkt_type) {}

  /// Send this session management packet "as is"
  inline void send_to(const char *dst_hostname,
                      const udp_config_t *udp_config) {
    assert(dst_hostname != NULL);

    UDPClient udp_client(dst_hostname, udp_config->mgmt_udp_port,
                         udp_config->drop_prob);
    ssize_t ret = udp_client.send((char *)this, sizeof(*this));
    _unused(ret);
    assert(ret == (ssize_t)sizeof(*this));
  }

  /**
   * @brief Send the response to this session management request packet, using
   * this packet as the response buffer. This function mutates the packet: it
   * flips the packet type to response, and fills in the response type.
   */
  inline void send_resp_mut(SessionMgmtErrType _err_type,
                            const udp_config_t *udp_config) {
    assert(session_mgmt_pkt_type_is_req(pkt_type));
    pkt_type = session_mgmt_pkt_type_req_to_resp(pkt_type);
    err_type = _err_type;

    send_to(client.hostname, udp_config);
  }
};
static_assert(sizeof(SessionMgmtPkt) < 1400,
              "Session management packet too large for UDP");

/// A hook created by the per-thread Rpc, and shared with the per-process Nexus.
/// All accesses must be done with @session_mgmt_mutex locked.
class SessionMgmtHook {
 public:
  uint8_t app_tid; /* App-level thread ID of the RPC that created this hook */
  std::mutex session_mgmt_mutex;
  volatile size_t session_mgmt_ev_counter; /* Number of session mgmt events */
  std::vector<SessionMgmtPkt *> session_mgmt_pkt_list;

  SessionMgmtHook() : session_mgmt_ev_counter(0) {}
};

}  // End ERpc

#endif  // ERPC_SESSION_MGMT_PKT_TYPE_H

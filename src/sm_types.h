#ifndef ERPC_SESSION_MGMT_PKT_TYPE_H
#define ERPC_SESSION_MGMT_PKT_TYPE_H

#include <mutex>
#include "common.h"
#include "transport.h"

namespace ERpc {

/// Maximum number of sessions (both as client and server) that can be created
/// by an Rpc through its lifetime. Increase this for more sessions.
static constexpr size_t kMaxSessionsPerThread = 4096;
static_assert(kMaxSessionsPerThread < std::numeric_limits<uint16_t>::max(), "");

static constexpr size_t kSecretBits = 32;  ///< Session secret for security
static_assert(kSecretBits <= 32, "");      // Secret must fit in 32 bits

// Invalid metadata values for session endpoint initialization
static constexpr uint16_t kInvalidSessionNum =
    std::numeric_limits<uint16_t>::max();
static constexpr uint32_t kInvalidSecret = 0;

enum class SessionState {
  kConnectInProgress,
  kConnected,  ///< The only state for server-side sessions
  kDisconnectInProgress,
  kDisconnected,  ///< Temporary state for the disconnected callback
};

/// Packet types used for session management
enum class SmPktType : int {
  kConnectReq,           ///< Session connect request
  kConnectResp,          ///< Session connect response
  kDisconnectReq,        ///< Session disconnect request
  kDisconnectResp,       ///< Session disconnect response
  kFaultResetPeerReq,    ///< Reset the remote ENet peer
  kFaultDropTxRemoteReq  ///< Drop a TX packet at the remote ENet peer
};

/// The types of responses to a session management packet
enum class SmErrType : int {
  kNoError,          ///< The only non-error error type
  kSrvDisconnected,  ///< The server session is disconnected
  kTooManySessions,  ///< Connect req failed because server is out of sessions
  kRecvsExhausted,   ///< Connect req failed because server is out of RECVs
  kOutOfMemory,      ///< Connect req failed because server is out of memory
  kRoutingResolutionFailure,  ///< Server failed to resolve client routing info
  kInvalidRemoteRpcId,
  kInvalidRemotePort,
  kInvalidTransport
};

/// Events generated for application-level session management handler
enum class SmEventType {
  kConnected,
  kConnectFailed,
  kDisconnected,
  kDisconnectFailed
};

typedef void (*sm_handler_t)(int, SmEventType, SmErrType, void *);

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
  }
  return std::string("[Invalid state]");
}

static std::string sm_pkt_type_str(SmPktType sm_pkt_type) {
  switch (sm_pkt_type) {
    case SmPktType::kConnectReq:
      return std::string("[Connect request]");
    case SmPktType::kConnectResp:
      return std::string("[Connect response]");
    case SmPktType::kDisconnectReq:
      return std::string("[Disconnect request]");
    case SmPktType::kDisconnectResp:
      return std::string("[Disconnect response]");
    case SmPktType::kFaultResetPeerReq:
      return std::string("[Reset peer request (fault injection)]");
    case SmPktType::kFaultDropTxRemoteReq:
      return std::string("[Drop TX remote (fault injection)]");
  };

  throw std::runtime_error("eRPC: Invalid session management packet type.");
}

/// Check if a session management packet type is valid
static bool sm_pkt_type_is_valid(SmPktType sm_pkt_type) {
  switch (sm_pkt_type) {
    case SmPktType::kConnectReq:
    case SmPktType::kConnectResp:
    case SmPktType::kDisconnectReq:
    case SmPktType::kDisconnectResp:
    case SmPktType::kFaultResetPeerReq:
    case SmPktType::kFaultDropTxRemoteReq:
      return true;
  }
  return false;
}

/// Check if a valid session management packet type is a request type. Use
/// the complement of this to check if a packet is a response.
static bool sm_pkt_type_is_req(SmPktType sm_pkt_type) {
  assert(sm_pkt_type_is_valid(sm_pkt_type));
  switch (sm_pkt_type) {
    case SmPktType::kConnectReq:
    case SmPktType::kDisconnectReq:
    case SmPktType::kFaultResetPeerReq:
    case SmPktType::kFaultDropTxRemoteReq:
      return true;
    case SmPktType::kConnectResp:
    case SmPktType::kDisconnectResp:
      return false;
  }

  throw std::runtime_error("eRPC: Invalid session management packet type.");
}

/// Return the response type for request type \p sm_pkt_type
static SmPktType sm_pkt_type_req_to_resp(SmPktType sm_pkt_type) {
  assert(sm_pkt_type_is_req(sm_pkt_type));
  switch (sm_pkt_type) {
    case SmPktType::kConnectReq:
      return SmPktType::kConnectResp;
    case SmPktType::kDisconnectReq:
      return SmPktType::kDisconnectResp;
    case SmPktType::kFaultResetPeerReq:
    case SmPktType::kFaultDropTxRemoteReq:
    case SmPktType::kConnectResp:
    case SmPktType::kDisconnectResp:
      break;
  }

  throw std::runtime_error("eRPC: Invalid session management packet type.");
}

/// Return true iff this request packet type has a response type. Some request
/// packet types don't have a response packet type.
static bool sm_pkt_type_req_has_resp(SmPktType sm_pkt_type) {
  assert(sm_pkt_type_is_req(sm_pkt_type));
  switch (sm_pkt_type) {
    case SmPktType::kConnectReq:
    case SmPktType::kDisconnectReq:
      return true;
    case SmPktType::kFaultResetPeerReq:
    case SmPktType::kFaultDropTxRemoteReq:
      return false;
    case SmPktType::kConnectResp:
    case SmPktType::kDisconnectResp:
      break;
  }

  throw std::runtime_error("eRPC: Invalid session management packet type.");
}

static bool sm_err_type_is_valid(SmErrType err_type) {
  switch (err_type) {
    case SmErrType::kNoError:
    case SmErrType::kSrvDisconnected:
    case SmErrType::kTooManySessions:
    case SmErrType::kRecvsExhausted:
    case SmErrType::kOutOfMemory:
    case SmErrType::kRoutingResolutionFailure:
    case SmErrType::kInvalidRemoteRpcId:
    case SmErrType::kInvalidRemotePort:
    case SmErrType::kInvalidTransport:
      return true;
  }
  return false;
}

static std::string sm_err_type_str(SmErrType err_type) {
  assert(sm_err_type_is_valid(err_type));

  switch (err_type) {
    case SmErrType::kNoError:
      return std::string("[No error]");
    case SmErrType::kSrvDisconnected:
      return std::string("[Server disconnected]");
    case SmErrType::kTooManySessions:
      return std::string("[Too many sessions]");
    case SmErrType::kRecvsExhausted:
      return std::string("[RECVs exhausted]");
    case SmErrType::kOutOfMemory:
      return std::string("[Out of memory]");
    case SmErrType::kRoutingResolutionFailure:
      return std::string("[Routing resolution failure]");
    case SmErrType::kInvalidRemoteRpcId:
      return std::string("[Invalid remote Rpc ID]");
    case SmErrType::kInvalidRemotePort:
      return std::string("[Invalid remote port]");
    case SmErrType::kInvalidTransport:
      return std::string("[Invalid transport]");
  }

  throw std::runtime_error("eRPC: Invalid session management error type");
}

static std::string sm_event_type_str(SmEventType event_type) {
  switch (event_type) {
    case SmEventType::kConnected:
      return std::string("[Connected]");
    case SmEventType::kConnectFailed:
      return std::string("[Connect failed]");
    case SmEventType::kDisconnected:
      return std::string("[Disconnected]");
    case SmEventType::kDisconnectFailed:
      return std::string("[kDisconnect failed]");
  }
  return std::string("[Invalid event type]");
}

/// Basic metadata about a session end point. This is sent in session management
/// packets.
class SessionEndpoint {
 public:
  Transport::TransportType transport_type;
  char hostname[kMaxHostnameLen];  ///< Hostname of this endpoint
  uint8_t phy_port;                ///< Fabric port used by this endpoint
  uint8_t rpc_id;                  ///< ID of the Rpc that created this endpoint
  uint16_t session_num;  ///< The session number of this endpoint in its Rpc
  uint32_t secret : kSecretBits;        ///< Secret for both session endpoints
  Transport::RoutingInfo routing_info;  ///< Endpoint's routing info

  // Fill invalid metadata to aid debugging
  SessionEndpoint() {
    memset(static_cast<void *>(hostname), 0, sizeof(hostname));
    phy_port = kInvalidPhyPort;
    rpc_id = kInvalidRpcId;
    session_num = kInvalidSessionNum;
    secret = kInvalidSecret;
    memset(static_cast<void *>(&routing_info), 0, sizeof(routing_info));
  }

  /// Return a string with a name for this session endpoint, containing
  /// its hostname, Rpc ID, and the session number.
  inline std::string name() const {
    std::ostringstream ret;
    std::string session_num_str = (session_num == kInvalidSessionNum)
                                      ? "XX"
                                      : std::to_string(session_num);

    ret << "[H: " << trim_hostname(hostname)
        << ", R: " << std::to_string(rpc_id) << ", S: " << session_num_str
        << "]";
    return ret.str();
  }

  /// Return a string with the name of the Rpc hosting this session endpoint.
  inline std::string rpc_name() const {
    std::ostringstream ret;
    ret << "[H: " << trim_hostname(hostname)
        << ", R: " << std::to_string(rpc_id) << "]";
    return ret.str();
  }

  /// Compare two endpoints. RoutingInfo is left out because the SessionEndpoint
  /// object in session managament packets may not have routing info.
  bool operator==(const SessionEndpoint &other) const {
    return transport_type == other.transport_type &&
           strcmp(hostname, other.hostname) == 0 &&
           phy_port == other.phy_port && rpc_id == other.rpc_id &&
           session_num == other.session_num && secret == other.secret;
  }
};

/// General-purpose session management packet sent by both Rpc clients and
/// servers. This is pretty large (~500 bytes), so use sparingly.
class SmPkt {
 public:
  SmPktType pkt_type;
  SmErrType err_type;  ///< Error type, for responses only
  size_t gen_data;     ///< General-purpose data

  SessionEndpoint client, server;  ///< Endpoint metadata

  bool is_req() const { return sm_pkt_type_is_req(pkt_type); }
};

}  // End ERpc

#endif  // ERPC_SESSION_MGMT_PKT_TYPE_H

#ifndef ERPC_SESSION_MGMT_PKT_TYPE_H
#define ERPC_SESSION_MGMT_PKT_TYPE_H

#include <mutex>
#include "common.h"
#include "transport.h"

namespace erpc {

/// Maximum number of sessions (both as client and server) that can be created
/// by an Rpc through its lifetime. Increase this for more sessions.
static constexpr size_t kMaxSessionsPerThread = 4096;
static_assert(kMaxSessionsPerThread < std::numeric_limits<uint16_t>::max(), "");

// Invalid metadata values for session endpoint initialization
static constexpr uint16_t kInvalidSessionNum =
    std::numeric_limits<uint16_t>::max();
static constexpr uint32_t kInvalidSecret = 0;

// A cluster-wide unique token for each session
typedef size_t sm_uniq_token_t;

enum class SessionState {
  kConnectInProgress,  ///< Client-only state, connect request is in flight
  kConnected,
  kDisconnectInProgress,  ///< Client-only state, disconnect req is in flight
  kResetInProgress,       ///< A session reset is in progress
};

/// Packet types used for session management
enum class SmPktType : int {
  kConnectReq,      ///< Session connect request
  kConnectResp,     ///< Session connect response
  kDisconnectReq,   ///< Session disconnect request
  kDisconnectResp,  ///< Session disconnect response
};

/// The types of responses to a session management packet
enum class SmErrType : int {
  kNoError,          ///< The only non-error error type
  kSrvDisconnected,  ///< The control-path connection to the server failed
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
      return "[Connect in progress]";
    case SessionState::kConnected:
      return "[Connected]";
    case SessionState::kDisconnectInProgress:
      return "[Disconnect in progress]";
    case SessionState::kResetInProgress:
      return "[Reset in progress]";
  }
  return "[Invalid state]";
}

static std::string sm_pkt_type_str(SmPktType sm_pkt_type) {
  switch (sm_pkt_type) {
    case SmPktType::kConnectReq:
      return "[Connect request]";
    case SmPktType::kConnectResp:
      return "[Connect response]";
    case SmPktType::kDisconnectReq:
      return "[Disconnect request]";
    case SmPktType::kDisconnectResp:
      return "[Disconnect response]";
  };

  throw std::runtime_error("Invalid session management packet type.");
}

/// Check if a session management packet type is valid
static bool sm_pkt_type_is_valid(SmPktType sm_pkt_type) {
  switch (sm_pkt_type) {
    case SmPktType::kConnectReq:
    case SmPktType::kConnectResp:
    case SmPktType::kDisconnectReq:
    case SmPktType::kDisconnectResp:
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
      return true;
    case SmPktType::kConnectResp:
    case SmPktType::kDisconnectResp:
      return false;
  }

  throw std::runtime_error("Invalid session management packet type.");
}

/// Return the response type for request type \p sm_pkt_type
static SmPktType sm_pkt_type_req_to_resp(SmPktType sm_pkt_type) {
  assert(sm_pkt_type_is_req(sm_pkt_type));
  switch (sm_pkt_type) {
    case SmPktType::kConnectReq:
      return SmPktType::kConnectResp;
    case SmPktType::kDisconnectReq:
      return SmPktType::kDisconnectResp;
    case SmPktType::kConnectResp:
    case SmPktType::kDisconnectResp:
      break;
  }

  throw std::runtime_error("Invalid session management packet type.");
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
      return "[No error]";
    case SmErrType::kSrvDisconnected:
      return "[Server disconnected]";
    case SmErrType::kTooManySessions:
      return "[Too many sessions]";
    case SmErrType::kRecvsExhausted:
      return "[RECVs exhausted]";
    case SmErrType::kOutOfMemory:
      return "[Out of memory]";
    case SmErrType::kRoutingResolutionFailure:
      return "[Routing resolution failure]";
    case SmErrType::kInvalidRemoteRpcId:
      return "[Invalid remote Rpc ID]";
    case SmErrType::kInvalidRemotePort:
      return "[Invalid remote port]";
    case SmErrType::kInvalidTransport:
      return "[Invalid transport]";
  }

  throw std::runtime_error("Invalid session management error type");
}

static std::string sm_event_type_str(SmEventType event_type) {
  switch (event_type) {
    case SmEventType::kConnected:
      return "[Connected]";
    case SmEventType::kConnectFailed:
      return "[Connect failed]";
    case SmEventType::kDisconnected:
      return "[Disconnected]";
    case SmEventType::kDisconnectFailed:
      return "[kDisconnect failed]";
  }
  return "[Invalid event type]";
}

/// Basic metadata about a session end point, sent in session management packets
class SessionEndpoint {
 public:
  Transport::TransportType transport_type;
  char hostname[kMaxHostnameLen];  ///< Hostname of this endpoint
  uint8_t phy_port;                ///< Fabric port used by this endpoint
  uint8_t rpc_id;                  ///< ID of the Rpc that created this endpoint
  uint16_t session_num;  ///< The session number of this endpoint in its Rpc
  Transport::RoutingInfo routing_info;  ///< Endpoint's routing info

  // Fill invalid metadata to aid debugging
  SessionEndpoint() {
    memset(static_cast<void *>(hostname), 0, sizeof(hostname));
    phy_port = kInvalidPhyPort;
    rpc_id = kInvalidRpcId;
    session_num = kInvalidSessionNum;
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
           session_num == other.session_num;
  }
};

/// General-purpose session management packet sent by both Rpc clients and
/// servers. This is pretty large (~500 bytes), so use sparingly.
class SmPkt {
 public:
  SmPktType pkt_type;
  SmErrType err_type;              ///< Error type, for responses only
  sm_uniq_token_t uniq_token;      ///< The token for this session
  SessionEndpoint client, server;  ///< Endpoint metadata

  std::string to_string() const {
    std::ostringstream ret;
    ret << sm_pkt_type_str(pkt_type) << ", " << sm_err_type_str(err_type)
        << ", client: " << client.name() << ", server: " << server.name();
    return ret.str();
  }

  bool is_req() const { return sm_pkt_type_is_req(pkt_type); }
  bool is_resp() const { return !is_req(); }
};

static SmPkt sm_construct_resp(const SmPkt &req_sm_pkt, SmErrType err_type) {
  SmPkt resp_sm_pkt = req_sm_pkt;
  resp_sm_pkt.pkt_type = sm_pkt_type_req_to_resp(req_sm_pkt.pkt_type);
  resp_sm_pkt.err_type = err_type;
  return resp_sm_pkt;
}

}  // End erpc

#endif  // ERPC_SESSION_MGMT_PKT_TYPE_H

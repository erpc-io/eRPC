#pragma once

#include <mutex>
#include "common.h"
#include "transport.h"

namespace erpc {

/// Packet credits. This must be a power of two for fast matching of packet
/// numbers to their position in the TX timestamp array.
static constexpr size_t kSessionCredits = 8;
static_assert(is_power_of_two(kSessionCredits), "");

/// Request window size. This must be a power of two for fast multiplication and
/// modulo calculation during request number assignment and slot number
/// decoding, respectively.
static constexpr size_t kSessionReqWindow = 8;
static_assert(is_power_of_two(kSessionReqWindow), "");

// Invalid metadata values for session endpoint initialization
static constexpr uint16_t kInvalidSessionNum = UINT16_MAX;

// A cluster-wide unique token for each session, generated on session creation
typedef size_t conn_req_uniq_token_t;

enum class SessionState {
  kConnectInProgress,     ///< Client-only state, connect request is in flight
  kConnected,             ///< Session is successfully connected
  kDisconnectInProgress,  ///< Client-only state, disconnect req is in flight
  kResetInProgress,       ///< A session reset is in progress
};

/// Packet types used for session management
enum class SmPktType : int {
  kPingReq,         ///< Ping request
  kPingResp,        ///< Ping response
  kConnectReq,      ///< Session connect request
  kConnectResp,     ///< Session connect response
  kDisconnectReq,   ///< Session disconnect request
  kDisconnectResp,  ///< Session disconnect response
};

/// The types of responses to a session management packet
enum class SmErrType : int {
  kNoError,          ///< The only non-error error type
  kSrvDisconnected,  ///< The control-path connection to the server failed
  kRingExhausted,    ///< Connect req failed because server is out of ring bufs
  kOutOfMemory,      ///< Connect req failed because server is out of memory
  kRoutingResolutionFailure,  ///< Server failed to resolve client routing info
  kInvalidRemoteRpcId,  ///< Connect req failed because remote RPC ID was wrong
  kInvalidTransport     ///< Connect req failed because of transport mismatch
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
    case SessionState::kConnectInProgress: return "[Connect in progress]";
    case SessionState::kConnected: return "[Connected]";
    case SessionState::kDisconnectInProgress: return "[Disconnect in progress]";
    case SessionState::kResetInProgress: return "[Reset in progress]";
  }
  return "[Invalid state]";
}

static std::string sm_pkt_type_str(SmPktType sm_pkt_type) {
  switch (sm_pkt_type) {
    case SmPktType::kPingReq: return "[Ping request]";
    case SmPktType::kPingResp: return "[Ping response]";
    case SmPktType::kConnectReq: return "[Connect request]";
    case SmPktType::kConnectResp: return "[Connect response]";
    case SmPktType::kDisconnectReq: return "[Disconnect request]";
    case SmPktType::kDisconnectResp: return "[Disconnect response]";
  };

  throw std::runtime_error("Invalid session management packet type.");
}

/// Check if a session management packet type is valid
static bool sm_pkt_type_is_valid(SmPktType sm_pkt_type) {
  switch (sm_pkt_type) {
    case SmPktType::kPingReq:
    case SmPktType::kPingResp:
    case SmPktType::kConnectReq:
    case SmPktType::kConnectResp:
    case SmPktType::kDisconnectReq:
    case SmPktType::kDisconnectResp: return true;
  }
  return false;
}

/// Check if a valid session management packet type is a request type. Use
/// the complement of this to check if a packet is a response.
static bool sm_pkt_type_is_req(SmPktType sm_pkt_type) {
  assert(sm_pkt_type_is_valid(sm_pkt_type));
  switch (sm_pkt_type) {
    case SmPktType::kPingReq:
    case SmPktType::kConnectReq:
    case SmPktType::kDisconnectReq: return true;
    case SmPktType::kPingResp:
    case SmPktType::kConnectResp:
    case SmPktType::kDisconnectResp: return false;
  }

  throw std::runtime_error("Invalid session management packet type.");
}

/// Return the response type for request type \p sm_pkt_type
static SmPktType sm_pkt_type_req_to_resp(SmPktType sm_pkt_type) {
  assert(sm_pkt_type_is_req(sm_pkt_type));
  switch (sm_pkt_type) {
    case SmPktType::kPingReq: return SmPktType::kPingResp;
    case SmPktType::kConnectReq: return SmPktType::kConnectResp;
    case SmPktType::kDisconnectReq: return SmPktType::kDisconnectResp;
    case SmPktType::kPingResp:
    case SmPktType::kConnectResp:
    case SmPktType::kDisconnectResp: break;
  }

  throw std::runtime_error("Invalid session management packet type.");
}

static bool sm_err_type_is_valid(SmErrType err_type) {
  switch (err_type) {
    case SmErrType::kNoError:
    case SmErrType::kSrvDisconnected:
    case SmErrType::kRingExhausted:
    case SmErrType::kOutOfMemory:
    case SmErrType::kRoutingResolutionFailure:
    case SmErrType::kInvalidRemoteRpcId:
    case SmErrType::kInvalidTransport: return true;
  }
  return false;
}

static std::string sm_err_type_str(SmErrType err_type) {
  assert(sm_err_type_is_valid(err_type));

  switch (err_type) {
    case SmErrType::kNoError: return "[No error]";
    case SmErrType::kSrvDisconnected: return "[Server disconnected]";
    case SmErrType::kRingExhausted: return "[Ring buffers exhausted]";
    case SmErrType::kOutOfMemory: return "[Out of memory]";
    case SmErrType::kRoutingResolutionFailure:
      return "[Routing resolution failure]";
    case SmErrType::kInvalidRemoteRpcId: return "[Invalid remote Rpc ID]";
    case SmErrType::kInvalidTransport: return "[Invalid transport]";
  }

  throw std::runtime_error("Invalid session management error type");
}

static std::string sm_event_type_str(SmEventType event_type) {
  switch (event_type) {
    case SmEventType::kConnected: return "[Connected]";
    case SmEventType::kConnectFailed: return "[Connect failed]";
    case SmEventType::kDisconnected: return "[Disconnected]";
    case SmEventType::kDisconnectFailed: return "[kDisconnect failed]";
  }
  return "[Invalid event type]";
}

/// Basic metadata about a session end point, sent in session management packets
class SessionEndpoint {
 public:
  TransportType transport_type;
  char hostname[kMaxHostnameLen];  ///< DNS-resolvable hostname
  uint16_t sm_udp_port;            ///< Management UDP port
  uint8_t rpc_id;                  ///< ID of the owner
  uint16_t session_num;  ///< The session number of this endpoint in its Rpc
  Transport::RoutingInfo routing_info;  ///< Endpoint's routing info

  SessionEndpoint() {
    memset(static_cast<void *>(hostname), 0, sizeof(hostname));
    sm_udp_port = 0;  // UDP port 0 is naturally invalid
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

    ret << "[H: " << trim_hostname(hostname) << ":"
        << std::to_string(sm_udp_port) << ", R: " << std::to_string(rpc_id)
        << ", S: " << session_num_str << "]";
    return ret.str();
  }

  /// Return a string with the name of the Rpc hosting this session endpoint.
  inline std::string rpc_name() const {
    std::ostringstream ret;
    ret << "[H: " << trim_hostname(hostname) << ":"
        << std::to_string(sm_udp_port) << ", R: " << std::to_string(rpc_id)
        << "]";
    return ret.str();
  }

  /// Compare two endpoints. RoutingInfo is left out because the SessionEndpoint
  /// object in session managament packets may not have routing info.
  bool operator==(const SessionEndpoint &other) const {
    return transport_type == other.transport_type &&
           strcmp(hostname, other.hostname) == 0 &&
           sm_udp_port == other.sm_udp_port && rpc_id == other.rpc_id &&
           session_num == other.session_num;
  }
};

/// General-purpose session management packet sent by both Rpc clients and
/// servers. This is pretty large (~500 bytes), so use sparingly.
class SmPkt {
 public:
  SmPktType pkt_type;
  SmErrType err_type;                ///< Error type, for responses only
  conn_req_uniq_token_t uniq_token;  ///< The token for this session
  SessionEndpoint client, server;    ///< Endpoint metadata

  std::string to_string() const {
    std::ostringstream ret;
    ret << sm_pkt_type_str(pkt_type) << ", " << sm_err_type_str(err_type)
        << ", client: " << client.name() << ", server: " << server.name();
    return ret.str();
  }

  SmPkt() {}
  SmPkt(SmPktType pkt_type, SmErrType err_type,
        conn_req_uniq_token_t uniq_token, SessionEndpoint client,
        SessionEndpoint server)
      : pkt_type(pkt_type),
        err_type(err_type),
        uniq_token(uniq_token),
        client(client),
        server(server) {}

  // A ping request is a management packet where most fields are invalid
  SmPkt make_ping_req(TransportType transport_type,
                      const std::string &server_hostname,
                      const std::string &local_hostname) {
    SmPkt req;
    req.pkt_type = SmPktType::kPingReq;
    req.err_type = SmErrType::kNoError;
    req.uniq_token = 0;

    // The Rpc ID, session number, and UDP port in req's session endpoints is
    // already invalid.
    strcpy(req.client.hostname, local_hostname.c_str());
    req.client.transport_type = transport_type;

    strcpy(req.server.hostname, server_hostname.c_str());
    req.server.transport_type = transport_type;

    return req;
  }

  // The response to a ping is the same packet but with packet type switched
  SmPkt make_ping_resp(const SmPkt &ping_req) {
    SmPkt ping_resp = ping_req;
    ping_resp.pkt_type = SmPktType::kPingResp;
    return ping_resp;
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
}  // namespace erpc

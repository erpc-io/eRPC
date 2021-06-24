#pragma once

#include <mutex>
#include "common.h"
#include "transport.h"

namespace erpc {

/// Packet credits. This must be a power of two for fast matching of packet
/// numbers to their position in the TX timestamp array.
static constexpr size_t kSessionCredits = 32;
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
  kUnblock,         /// Unblock the session management request blocked on recv()
  kPingReq,         /// Heartbeat ping request
  kPingResp,        /// Heartbeat ping response
  kConnectReq,      /// Request to connect an eRPC session
  kConnectResp,     /// Response for the eRPC session connection request
  kDisconnectReq,   /// Request to disconnect an eRPC session
  kDisconnectResp,  /// Response for the eRPC session disconnect request
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
    case SmPktType::kUnblock: return "[Unblock SM thread request]";
    case SmPktType::kPingReq: return "[Heartbeat ping request]";
    case SmPktType::kPingResp: return "[Heartbeat ping response]";
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
    case SmPktType::kUnblock:
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
    case SmPktType::kUnblock:
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
    case SmPktType::kUnblock:
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
  TransportType transport_type_;
  char hostname_[kMaxHostnameLen];  ///< DNS-resolvable hostname
  uint16_t sm_udp_port_;            ///< Management UDP port
  uint8_t rpc_id_;                  ///< ID of the owner
  uint16_t session_num_;  ///< The session number of this endpoint in its Rpc
  Transport::routing_info_t routing_info_;  ///< Endpoint's routing info

  SessionEndpoint() {
    memset(static_cast<void *>(hostname_), 0, sizeof(hostname_));
    sm_udp_port_ = 0;  // UDP port 0 is naturally invalid
    rpc_id_ = kInvalidRpcId;
    session_num_ = kInvalidSessionNum;
    memset(static_cast<void *>(&routing_info_), 0, sizeof(routing_info_));
  }

  /// Return this endpoint's URI
  std::string uri() const {
    return std::string(hostname_) + ":" + std::to_string(sm_udp_port_);
  }

  /// Return a string with a name for this session endpoint, containing
  /// its hostname, Rpc ID, and the session number.
  inline std::string name() const {
    std::ostringstream ret;
    std::string session_num_str = (session_num_ == kInvalidSessionNum)
                                      ? "XX"
                                      : std::to_string(session_num_);

    ret << "[H: " << trim_hostname(hostname_) << ":"
        << std::to_string(sm_udp_port_) << ", R: " << std::to_string(rpc_id_)
        << ", S: " << session_num_str << "]";
    return ret.str();
  }

  /// Return a string with the name of the Rpc hosting this session endpoint.
  inline std::string rpc_name() const {
    std::ostringstream ret;
    ret << "[H: " << trim_hostname(hostname_) << ":"
        << std::to_string(sm_udp_port_) << ", R: " << std::to_string(rpc_id_)
        << "]";
    return ret.str();
  }

  /// Compare two endpoints. Routing info is left out: the SessionEndpoint
  /// object in session managament packets may not have routing info.
  bool operator==(const SessionEndpoint &other) const {
    return transport_type_ == other.transport_type_ &&
           strcmp(hostname_, other.hostname_) == 0 &&
           sm_udp_port_ == other.sm_udp_port_ && rpc_id_ == other.rpc_id_ &&
           session_num_ == other.session_num_;
  }
};

/// General-purpose session management packet sent by both Rpc clients and
/// servers. This is pretty large (~500 bytes), so use sparingly.
class SmPkt {
 public:
  SmPktType pkt_type_;
  SmErrType err_type_;                ///< Error type, for responses only
  conn_req_uniq_token_t uniq_token_;  ///< The token for this session
  SessionEndpoint client_, server_;   ///< Endpoint metadata

  std::string to_string() const {
    std::ostringstream ret;
    if (pkt_type_ != SmPktType::kUnblock) {
      ret << sm_pkt_type_str(pkt_type_) << ", " << sm_err_type_str(err_type_)
          << ", client: " << client_.name() << ", server: " << server_.name();
    } else {
      ret << sm_pkt_type_str(pkt_type_);
    }
    return ret.str();
  }

  SmPkt() {}
  SmPkt(SmPktType pkt_type, SmErrType err_type,
        conn_req_uniq_token_t uniq_token, SessionEndpoint client,
        SessionEndpoint server)
      : pkt_type_(pkt_type),
        err_type_(err_type),
        uniq_token_(uniq_token),
        client_(client),
        server_(server) {}

  /// Construct a response to a heartbeat ping request
  static SmPkt make_ping_resp(const SmPkt &ping_req) {
    // The response to a ping is the same packet but with packet type switched
    SmPkt ping_resp = ping_req;
    ping_resp.pkt_type_ = SmPktType::kPingResp;
    return ping_resp;
  }

  /// Construct a SmPkt to unblock a session management thread blocked on
  /// receving a session management packet
  static SmPkt make_unblock_req() {
    SmPkt ret;
    ret.pkt_type_ = SmPktType::kUnblock;
    return ret;
  }

  bool is_req() const { return sm_pkt_type_is_req(pkt_type_); }
  bool is_resp() const { return !is_req(); }
};

static SmPkt sm_construct_resp(const SmPkt &req_sm_pkt, SmErrType err_type) {
  SmPkt resp_sm_pkt = req_sm_pkt;
  resp_sm_pkt.pkt_type_ = sm_pkt_type_req_to_resp(req_sm_pkt.pkt_type_);
  resp_sm_pkt.err_type_ = err_type;
  return resp_sm_pkt;
}

/// A work item exchanged between an Rpc thread and an SM thread. This does
/// not have any Nexus-related members, so it's outside the Nexus class.
class SmWorkItem {
  enum class Reset { kFalse, kTrue };

 public:
  SmWorkItem(uint8_t rpc_id, SmPkt sm_pkt)
      : reset_(Reset::kFalse), rpc_id_(rpc_id), sm_pkt_(sm_pkt) {}

  SmWorkItem(std::string reset_rem_hostname)
      : reset_(Reset::kTrue),
        rpc_id_(kInvalidRpcId),
        reset_rem_hostname_(reset_rem_hostname) {}

  bool is_reset() const { return reset_ == Reset::kTrue; }

  const Reset reset_;     ///< Is this work item a reset?
  const uint8_t rpc_id_;  ///< The local Rpc ID, invalid for reset work items

  SmPkt sm_pkt_;  ///< The session management packet, for non-reset work items

  /// The remote hostname to reset, valid for reset work items
  std::string reset_rem_hostname_;
};
}  // namespace erpc

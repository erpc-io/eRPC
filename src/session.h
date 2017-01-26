#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include <limits>
#include <mutex>
#include <queue>
#include <string>

#include "common.h"
#include "session_mgmt_types.h"
#include "transport_types.h"
#include "util/udp_client.h"

namespace ERpc {

/*
 * Maximum number of sessions (both as client and server) that can be created
 * by a thread through its lifetime. This is small only for testing; several
 * million sessions should be fine.
 */
static const size_t kMaxSessionsPerThread = 1024;
static_assert(kMaxSessionsPerThread < std::numeric_limits<uint32_t>::max(),
              "Session number must fit in 32 bits");

/*
 * Invalid values that need to be filled in session metadata.
 */
static const uint8_t kInvalidAppTid = std::numeric_limits<uint8_t>::max();
static const uint32_t kInvalidSessionNum = std::numeric_limits<uint32_t>::max();
static const uint64_t kInvalidStartSeq = std::numeric_limits<uint64_t>::max();
static const uint8_t kInvalidPhyPort = std::numeric_limits<uint8_t>::max();

/**
 * @brief Session state that can only go forward.
 */
enum class SessionState {
  kConnectInProgress,
  kConnected, /*!< The only state for server-side sessions */
  kDisconnectInProgress,
  kDisconnected, /*!< Temporary state just for the disconnected callback */
  kError         /*!< Only allowed for client-side sessions */
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

/**
 * @brief Events generated for application-level session management handler
 */
enum class SessionMgmtEventType {
  kConnected,
  kConnectFailed,
  kDisconnected,
  kDisconnectFailed
};

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

/**
 * @brief Basic info about a session emd point filled in during initialization.
 */
class SessionMetadata {
 public:
  TransportType transport_type;
  char hostname[kMaxHostnameLen];
  uint8_t app_tid;   ///< TID of the Rpc that created this end point
  uint8_t phy_port;  ///< Fabric port used by this end point
  uint32_t session_num;
  uint64_t start_seq;
  RoutingInfo routing_info;

  /* Fill invalid metadata to aid debugging */
  SessionMetadata() {
    transport_type = TransportType::kInvalidTransport;
    memset((void *)hostname, 0, sizeof(hostname));
    app_tid = kInvalidAppTid;
    phy_port = kInvalidPhyPort;
    session_num = kInvalidSessionNum;
    start_seq = kInvalidStartSeq;
    memset((void *)&routing_info, 0, sizeof(routing_info));
  }

  /**
   * @brief Return a string with a name for this session end point, containing
   * its hostname, Rpc TID, and the session number.
   */
  inline std::string name() {
    std::string ret;
    ret += std::string("[H: "); /* Hostname */
    ret += std::string(hostname);
    ret += std::string(", R: "); /* Rpc */
    ret += std::to_string(app_tid);
    ret += std::string(", S: "); /* Session */
    if (session_num == kInvalidSessionNum) {
      ret += std::string("XX");
    } else {
      ret += std::to_string(session_num);
    }
    ret += std::string("]");

    return ret;
  }

  /**
   * @brief Return a string with the name of the Rpc hosting this session
   * end point.
   */
  inline std::string rpc_name() {
    std::string ret;
    ret += std::string("[H: "); /* Hostname */
    ret += std::string(hostname);
    ret += std::string(", R: "); /* Rpc */
    ret += std::to_string(app_tid);
    ret += std::string("]");

    return ret;
  }

  /**
   * @brief Compare the location fields of two SessionMetadata objects. This
   * does not account for non-location fields (e.g., fabric port, routing info).
   */
  bool operator==(const SessionMetadata &other) {
    return strcmp(hostname, other.hostname) == 0 && app_tid == other.app_tid &&
           session_num == other.session_num;
  }
};

/**
 * @brief General-purpose session management packet sent by both Rpc clients
 * and servers. This is pretty large (~500 bytes), so use sparingly.
 */
class SessionMgmtPkt {
 public:
  SessionMgmtPktType pkt_type;
  SessionMgmtErrType err_type; /* For responses only */

  /*
   * Each session management packet contains two copies of session metadata,
   * filled in by the client and server Rpc.
   */
  SessionMetadata client, server;

  SessionMgmtPkt() {}
  SessionMgmtPkt(SessionMgmtPktType pkt_type) : pkt_type(pkt_type) {}

  /**
   * @brief Send this session management packet "as is" to \p dst_hostname on
   * port \p mgmt_udp_port.
   */
  inline void send_to(const char *dst_hostname,
                      const udp_config_t *udp_config) {
    assert(dst_hostname != NULL);

    UDPClient udp_client(dst_hostname, udp_config->mgmt_udp_port,
                         udp_config->drop_prob);
    ssize_t ret = udp_client.send((char *)this, sizeof(*this));
    _unused(ret);
    assert(ret == sizeof(*this));
  }

  /**
   * @brief Send the response to this session management request packet, using
   * this packet as the response buffer.
   *
   * This function mutates the packet: it flips the packet type to response,
   * and fills in the response type.
   */
  inline void send_resp_mut(SessionMgmtErrType _err_type,
                            const udp_config_t *udp_config) {
    assert(session_mgmt_is_pkt_type_req(pkt_type));
    pkt_type = session_mgmt_pkt_type_req_to_resp(pkt_type);
    err_type = _err_type;

    send_to(client.hostname, udp_config);
  }
};
static_assert(sizeof(SessionMgmtPkt) < 1400,
              "Session management packet too large for UDP");

/**
 * @brief A one-to-one session class for all transports
 */
class Session {
 public:
  enum class Role : bool { kServer, kClient };

  Session(Role role, SessionState state);
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

  Role role;
  SessionState state;
  SessionMetadata client, server;

  bool is_cc; /* Is congestion control enabled for this session? */
};

typedef void (*session_mgmt_handler_t)(Session *, SessionMgmtEventType,
                                       SessionMgmtErrType, void *);

/**
 * @brief An object created by the per-thread Rpc, and shared with the
 * per-process Nexus. All accesses must be done with @session_mgmt_mutex locked.
 */
class SessionMgmtHook {
 public:
  uint8_t app_tid; /* App-level thread ID of the RPC that created this hook */
  std::mutex session_mgmt_mutex;
  volatile size_t session_mgmt_ev_counter; /* Number of session mgmt events */
  std::vector<SessionMgmtPkt *> session_mgmt_pkt_list;

  SessionMgmtHook() : session_mgmt_ev_counter(0) {}
};

}  // End ERpc

#endif  // ERPC_SESSION_H

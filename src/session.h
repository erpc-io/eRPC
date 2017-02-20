#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include <limits>
#include <queue>
#include <sstream>
#include <string>

#include "common.h"
#include "session_mgmt_types.h"

namespace ERpc {

/// A one-to-one session class for all transports
class Session {
 public:
  static const size_t kSessionCredits = 8;  ///< Credits per session endpoint
  static const size_t kReqNumBits = 48;     ///< Bits for request number
  static const size_t kPktNumBits = 14;     ///< Bits for packet number

  enum class Role : bool { kServer, kClient };

  Session(Role role, SessionState state);
  ~Session();

  /// Enable congestion control for this session
  void enable_congestion_control() {
    assert(role == Role::kClient);
    client_info.is_cc = true;
  }

  /// Disable congestion control for this session
  void disable_congestion_control() {
    assert(role == Role::kClient);
    client_info.is_cc = false;
  }

  Role role;           ///< The role (server/client) of this session endpoint
  SessionState state;  ///< The management state of this session endpoint
  SessionEndpoint client, server;  ///< Read-only endpoint metadata

  size_t remote_credits = kSessionCredits;  ///< Current eRPC credits

  /* Information that is required only at the client endpoint */
  struct {
    uint64_t mgmt_req_tsc;  ///< Timestamp of the last management request
    bool is_cc = false;     ///< True if this session is congestion controlled
    uint64_t cur_req_num = 0;  ///< Current request number
    uint16_t cur_pkt_num = 0;  ///< Packet number in current request
  } client_info;
};

typedef void (*session_mgmt_handler_t)(Session *, SessionMgmtEventType,
                                       SessionMgmtErrType, void *);

}  // End ERpc

#endif  // ERPC_SESSION_H

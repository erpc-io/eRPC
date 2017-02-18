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
  enum class Role : bool { kServer, kClient };

  Session(Role role, SessionState state);
  ~Session();

  /// Enable congestion control for this session
  void enable_congestion_control() { is_cc = true; }

  /// Disable congestion control for this session
  void disable_congestion_control() { is_cc = false; }

  // Cold data
  Role role;           ///< The role (server/client) of this session endpoint
  SessionState state;  ///< The management state of this session endpoint
  SessionEndpoint client, server;  ///< The two endpoints of this session
  uint64_t mgmt_req_tsc;           ///< Timestamp of the last management request
  bool is_cc;  ///< True if congestion control is enabled for this session

  // Hot data
};

typedef void (*session_mgmt_handler_t)(Session *, SessionMgmtEventType,
                                       SessionMgmtErrType, void *);

}  // End ERpc

#endif  // ERPC_SESSION_H

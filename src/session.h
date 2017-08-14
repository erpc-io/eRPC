#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include <limits>
#include <mutex>
#include <queue>

#include "common.h"
#include "msg_buffer.h"
#include "ops.h"
#include "pkthdr.h"
#include "sm_types.h"
#include "sslot.h"
#include "util/buffer.h"
#include "util/fixed_vector.h"

namespace ERpc {

// Forward declarations for friendship. Prevent Nexus's background threads
// from accessing session members.
class IBTransport;

template <typename T>
class Rpc;

/// A one-to-one session class for all transports
class Session {
  friend class Rpc<IBTransport>;

 public:
  enum class Role : int { kServer, kClient };

  static constexpr size_t kSessionReqWindow = 8;  ///< *Request* window size
  static constexpr size_t kSessionCredits = 8;    ///< *Packet* credits

  /// Controls printing of credit exhausted alerts
  static constexpr size_t kCreditsExhaustedWarnLim = 1000000000;

  // Required for fast multiplication and modulo calculation during request
  // number assignment and slot number decoding, respectively.
  static_assert(is_power_of_two(kSessionReqWindow), "");

 private:
  Session(Role role, SessionState state);

  /// Session resources are freed in Rpc::bury_session(), so this is empty
  ~Session() {}

  inline bool is_client() const { return role == Role::kClient; }
  inline bool is_server() const { return role == Role::kServer; }
  inline bool is_connected() const { return state == SessionState::kConnected; }

  Role role;           ///< The role (server/client) of this session endpoint
  SessionState state;  ///< The management state of this session endpoint
  SessionEndpoint client, server;  ///< Read-only endpoint metadata

  SSlot sslot_arr[kSessionReqWindow];  ///< The session slots

  ///@{ Info saved for faster unconditional access
  Transport::RoutingInfo *remote_routing_info;
  uint16_t local_session_num;
  uint16_t remote_session_num;
  ///@}

  /// Information that is required only at the client endpoint
  struct {
    /// Free session slots. We could use a vector of SSlot pointers, but indices
    /// are useful in request number calculations.
    FixedVector<size_t, kSessionReqWindow> sslot_free_vec;

    size_t credits = kSessionCredits;  ///< Currently available credits

    /// Set to disable the disconnect callback. This is used when session
    /// connection fails despite getting an error-free connect response
    bool sm_callbacks_disabled = false;

    /// True if this session has a pending session management API request.
    /// This does not account for fault-injection requests.
    bool sm_api_req_pending = false;
  } client_info;

  struct {
    size_t credits_exhaused = 0;
  } dpath_stats;
};

}  // End ERpc

#endif  // ERPC_SESSION_H

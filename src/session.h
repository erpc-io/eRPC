#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include <limits>
#include <mutex>
#include <queue>

#include "common.h"
#include "msg_buffer.h"
#include "ops.h"
#include "pkthdr.h"
#include "session_mgmt_types.h"
#include "sslot.h"
#include "util/buffer.h"
#include "util/fixed_vector.h"

namespace ERpc {

/// A one-to-one session class for all transports
class Session {
 public:
  enum class Role : int {
    /* Weird numbers to help detect use of freed session pointers */
    kServer = 37,
    kClient = 95
  };

  static constexpr size_t kSessionReqWindow = 8;  ///< *Request* window size
  static constexpr size_t kSessionCredits = 8;    ///< *Packet* credits

  /*
   * Required for fast multiplication and modulo calculation during request
   * number assignment and slot number decoding, respectively.
   */
  static_assert(is_power_of_two(kSessionReqWindow), "");

  Session(Role role, SessionState state);

  /// Session resources are freed in Rpc::bury_session(), so this is empty
  ~Session() {}

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

  inline bool is_client() const { return role == Role::kClient; }
  inline bool is_server() const { return role == Role::kServer; }
  inline bool is_connected() const { return state == SessionState::kConnected; }

  std::mutex lock;     ///< A lock for protecting this session
  Role role;           ///< The role (server/client) of this session endpoint
  SessionState state;  ///< The management state of this session endpoint
  SessionEndpoint client, server;  ///< Read-only endpoint metadata

  size_t remote_credits = kSessionCredits;  ///< This session's current credits
  SSlot sslot_arr[kSessionReqWindow];       ///< The session slots
  FixedVector<size_t, kSessionReqWindow> sslot_free_vec;  ///< Free slots

  ///@{ Info saved for faster unconditional access
  RoutingInfo *remote_routing_info;
  uint16_t local_session_num;
  uint16_t remote_session_num;
  ///@}

  struct {
    size_t remote_credits_exhaused = 0;
  } dpath_stats;

  /// Information that is required only at the client endpoint
  struct {
    /// We disable the disconnect callback if session connection fails despite
    /// getting an error-free connect response
    bool sm_callbacks_disabled = false;

    uint64_t mgmt_req_tsc;  ///< Timestamp of the last management request
    bool is_cc = false;     ///< True if this session is congestion controlled
  } client_info;
};

}  // End ERpc

#endif  // ERPC_SESSION_H

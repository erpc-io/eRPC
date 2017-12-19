#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include <limits>
#include <mutex>
#include <queue>

#include "common.h"
#include "msg_buffer.h"
#include "ops.h"
#include "sm_types.h"
#include "sslot.h"
#include "util/buffer.h"
#include "util/fixed_vector.h"

namespace erpc {

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

  static constexpr size_t kSessionReqWindow = 8;  ///< Request window size
  static constexpr size_t kSessionCredits = 8;    ///< Packet credits

  /// Controls printing of credit exhausted alerts
  static constexpr size_t kCreditsExhaustedWarnLim = 1000000000;

  // Required for fast multiplication and modulo calculation during request
  // number assignment and slot number decoding, respectively.
  static_assert(is_power_of_two(kSessionReqWindow), "");

 private:
  Session(Role role, conn_req_uniq_token_t uniq_token)
      : role(role), uniq_token(uniq_token) {
    remote_routing_info =
        is_client() ? &server.routing_info : &client.routing_info;

    // Arrange the free slot vector so that slots are popped in order
    for (size_t i = 0; i < kSessionReqWindow; i++) {
      // Initialize session slot with index = sslot_i
      const size_t sslot_i = (kSessionReqWindow - 1 - i);
      SSlot &sslot = sslot_arr[sslot_i];

      // This buries all MsgBuffers
      memset(static_cast<void *>(&sslot), 0, sizeof(SSlot));

      sslot.prealloc_used = true;  // There's no user-allocated memory to free
      sslot.session = this;
      sslot.is_client = is_client();
      sslot.index = sslot_i;
      sslot.cur_req_num = sslot_i;  // 1st req num = (+kSessionReqWindow)

      if (is_client()) {
        sslot.client_info.cont_etid = kInvalidBgETid;  // Continuations in fg
      } else {
        sslot.server_info.req_type = kInvalidReqType;
      }

      client_info.sslot_free_vec.push_back(sslot_i);
    }
  }

  /// All session resources are freed by the owner Rpc
  ~Session() {}

  inline bool is_client() const { return role == Role::kClient; }
  inline bool is_server() const { return role == Role::kServer; }
  inline bool is_connected() const { return state == SessionState::kConnected; }

  const Role role;  ///< The role (server/client) of this session endpoint
  const conn_req_uniq_token_t uniq_token;  ///< A cluster-wide unique token
  SessionState state;  ///< The management state of this session endpoint
  SessionEndpoint client, server;  ///< Read-only endpoint metadata

  std::array<SSlot, kSessionReqWindow> sslot_arr;  ///< The session slots

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
    size_t sm_req_ts;  ///< Timestamp of the last session management request
  } client_info;

  struct {
    size_t credits_exhaused = 0;
  } dpath_stats;
};

}  // End erpc

#endif  // ERPC_SESSION_H

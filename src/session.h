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
#include "timely.h"
#include "util/buffer.h"
#include "util/fixed_vector.h"

namespace erpc {

// Forward declarations for friendship. Prevent Nexus's background threads
// from accessing session members.
class IBTransport;
class RawTransport;

template <typename T>
class Rpc;

/// A one-to-one session class for all transports
class Session {
  friend class Rpc<IBTransport>;
  friend class Rpc<RawTransport>;

 public:
  enum class Role : int { kServer, kClient };

 private:
  Session(Role role, conn_req_uniq_token_t uniq_token, double freq_ghz)
      : role(role), uniq_token(uniq_token) {
    remote_routing_info =
        is_client() ? &server.routing_info : &client.routing_info;

    if (kCC && is_client()) {
      client_info.timely_tx = Timely(freq_ghz);
      client_info.timely_rx = Timely(freq_ghz);
    }

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
    size_t credits = kSessionCredits;  ///< Currently available credits

    /// Free session slots. We could use sslot pointers, but indices are useful
    /// in request number calculation.
    FixedVector<size_t, kSessionReqWindow> sslot_free_vec;

    /// Requests that spill over kSessionReqWindow are queued here
    std::vector<enq_req_args_t> enq_req_backlog;

    size_t sm_req_ts;  ///< Timestamp of the last session management request
    Timely timely_tx, timely_rx;
  } client_info;
};

}  // End erpc

#endif  // ERPC_SESSION_H

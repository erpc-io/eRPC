#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include <limits>
#include <queue>
#include <sstream>
#include <string>

#include "common.h"
#include "session_mgmt_types.h"
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

  static const size_t kSessionReqWindow = 8;  ///< *Request* window size
  static const size_t kSessionCredits = 8;    ///< *Packet* credits per endpoint

  /*
   * Required for fast multiplication and modulo calculation during request
   * number assignment and slot number decoding, respectively.
   */
  static_assert(is_power_of_two(kSessionReqWindow), "");

  /// Session slot = metadata maintained about an Rpc
  struct sslot_t {
    bool in_use = false;  ///< True iff this slot is in use
    MsgBuffer rx_msgbuf;  ///< The RX MsgBuffer for this slot
    MsgBuffer tx_msgbuf;  ///< The TX MsgBuffer for this slot

    ///< A pre-allocated 4K packet buffer. XXX: unused
    Buffer _prealloc;
  };

  Session(Role role, SessionState state);

  /// Session resources are freed in bury_session(), so this is empty
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

  Role role;           ///< The role (server/client) of this session endpoint
  SessionState state;  ///< The management state of this session endpoint
  SessionEndpoint client, server;           ///< Read-only endpoint metadata
  size_t remote_credits = kSessionCredits;  ///< This session's current credits
  bool in_datapath_tx_work_queue;  ///< True iff session is in tx work queue
  sslot_t sslot_arr[kSessionReqWindow];                   ///< The session slots
  FixedVector<size_t, kSessionReqWindow> sslot_free_vec;  ///< Free slots

  /// Depending on this session's role, save a pointer to \p server's or
  /// \p client's RoutingInfo for faster unconditional access
  RoutingInfo *remote_routing_info;

  /// Information that is required only at the client endpoint
  struct {
    uint64_t mgmt_req_tsc;  ///< Timestamp of the last management request
    bool is_cc = false;     ///< True if this session is congestion controlled
  } client_info;
};

typedef void (*session_mgmt_handler_t)(Session *, SessionMgmtEventType,
                                       SessionMgmtErrType, void *);

}  // End ERpc

#endif  // ERPC_SESSION_H

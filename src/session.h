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
  /// Info maintained about a request or response message
  struct msg_info_t {
    Buffer pkt_buffer;  ///< Packet buffer. Also contains the \p req_num below.
    size_t data_bytes_sent = 0;  ///< Number of non-header bytes already sent
    size_t req_num;  ///< The req number for this message (also in pkt_buffer)

    msg_info_t() {}
    msg_info_t(Buffer pkt_buffer) : pkt_buffer(pkt_buffer) {}
  };

  enum class Role : bool { kServer, kClient };

  static const size_t kSessionCredits = 8;  ///< Credits per endpoint

  /// Max number of unexpected *requests* kept outstanding by this session
  static const size_t kSessionReqWindow = 8;

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
  SessionEndpoint client, server;           ///< Read-only endpoint metadata
  size_t remote_credits = kSessionCredits;  ///< This session's current credits

  msg_info_t msg_arr[kSessionReqWindow];
  FixedVector<size_t, kSessionReqWindow> msg_arr_free_vec;

  /// Information that is required only at the client endpoint
  struct {
    uint64_t mgmt_req_tsc;  ///< Timestamp of the last management request
    bool is_cc = false;     ///< True if this session is congestion controlled

    /// The next request number to use. Not a bitfield for simplicity.
    size_t next_req_num = 0;
  } client_info;
};

typedef void (*session_mgmt_handler_t)(Session *, SessionMgmtEventType,
                                       SessionMgmtErrType, void *);

}  // End ERpc

#endif  // ERPC_SESSION_H

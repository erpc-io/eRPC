#ifndef ERPC_SESSION_H
#define ERPC_SESSION_H

#include <limits>
#include <queue>

#include "common.h"
#include "msg_buffer.h"
#include "ops.h"
#include "pkthdr.h"
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

  /**
   * @brief Session slot metadata maintained about an Rpc
   *
   * This slot structure is used by both server and client sessions.
   *
   * The validity/existence of a request or response in a slot is inferred from
   * \p rx_msgbuf or \p tx_msgbuf. Doing so avoids maintaining additional
   * boolean fields (such as \em is_req_received and \em is_resp_generated).
   *
   * If either \p rx_msgbuf or \p tx_msgbuf is valid outside a function,
   * its packet header must contain the request type and number.
   */
  struct sslot_t {
    // Fields that are meaningful for both server and client mode sessions
    MsgBuffer rx_msgbuf;   ///< The RX MsgBuffer, valid if \p buf is not NULL
    MsgBuffer *tx_msgbuf;  ///< The TX MsgBuffer, valid if it is not NULL

    // Server-only fields

    /// The application's response, containing a preallocated MsgBuffer.
    app_resp_t app_resp;

    /// Return a string representation of this session slot, excluding
    /// \p app_resp
    std::string to_string() const {
      if (rx_msgbuf.buf == nullptr && tx_msgbuf == nullptr) {
        return "[Invalid]";
      }

      /*
       * Sanity check: If the RX and TX MsgBuffers are both valid, they should
       * contain identical request number and type.
       */
      if (rx_msgbuf.buf != nullptr && tx_msgbuf != nullptr) {
        assert(rx_msgbuf.get_req_num() == tx_msgbuf->get_req_num());
        assert(rx_msgbuf.get_req_type() == tx_msgbuf->get_req_type());
      }

      /* Extract the request number and type from either RX or TX MsgBuffer */
      std::string req_num_string, req_type_string;
      if (rx_msgbuf.buf != nullptr) {
        req_num_string = std::to_string(rx_msgbuf.get_req_num());
        req_type_string = std::to_string(rx_msgbuf.get_req_type());
      }

      if (tx_msgbuf != nullptr && rx_msgbuf.buf == nullptr) {
        req_num_string = std::to_string(tx_msgbuf->get_req_num());
        req_type_string = std::to_string(tx_msgbuf->get_req_type());
      }

      std::ostringstream ret;
      ret << "[req num" << req_num_string << ", "
          << "req type " << req_type_string << ", "
          << "rx_msgbuf " << rx_msgbuf.to_string() << ", "
          << "tx_msgbuf "
          << (tx_msgbuf == nullptr ? "0x0" : tx_msgbuf->to_string()) << "]";
      return ret.str();
    }
  };

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

  Role role;           ///< The role (server/client) of this session endpoint
  SessionState state;  ///< The management state of this session endpoint
  SessionEndpoint client, server;           ///< Read-only endpoint metadata
  size_t remote_credits = kSessionCredits;  ///< This session's current credits
  bool in_datapath_tx_work_queue = false;   ///< Is session in tx work queue?

  sslot_t sslot_arr[kSessionReqWindow];                   ///< The session slots
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
    uint64_t mgmt_req_tsc;  ///< Timestamp of the last management request
    bool is_cc = false;     ///< True if this session is congestion controlled
  } client_info;
};

typedef void (*session_mgmt_handler_t)(Session *, SessionMgmtEventType,
                                       SessionMgmtErrType, void *);

}  // End ERpc

#endif  // ERPC_SESSION_H

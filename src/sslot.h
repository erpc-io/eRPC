#pragma once

#include "msg_buffer.h"
#include "rpc_types.h"
#include "sm_types.h"

namespace erpc {

// Forward declarations for friendship
class Session;
class Nexus;

template <typename T>
class Rpc;

/// Session slot metadata maintained for an RPC by both client and server
class SSlot {
  friend class Session;
  friend class Nexus;
  friend class Rpc<CTransport>;
  friend class ReqHandle;

 public:
  SSlot() {}
  ~SSlot() {}

  // Server-only members. Exposed to req handlers, so not kept in server struct.

  /// A preallocated msgbuf for single-packet responses
  MsgBuffer pre_resp_msgbuf;

  /// A non-preallocated msgbuf for possibly multi-packet responses
  MsgBuffer dyn_resp_msgbuf;

 private:
  // Members that are valid for both server and client
  Session *session;  ///< Pointer to this sslot's session

  /// True iff this sslot is a client sslot. sslot class does not have complete
  /// access to \p session, so we need this info separately.
  bool is_client;

  size_t index;  ///< Index of this sslot in the session's sslot_arr

  /// The request (client) or response (server) buffer. For client sslots, a
  /// non-null value indicates that the request is active/incomplete.
  MsgBuffer *tx_msgbuf;

  /// Info about the current request
  size_t cur_req_num;

  union {
    struct {
      MsgBuffer *resp_msgbuf;      ///< User-supplied response buffer
      erpc_cont_func_t cont_func;  ///< Continuation function for the request
      void *tag;                   ///< Tag of the request

      /// Number of packets sent. Packets up to (num_tx - 1) have been sent.
      size_t num_tx;

      /// Number of pkts received. Pkts up to (num_tx - 1) have been received.
      size_t num_rx;

      /// TSC at which we last sent or retransmitted a packet, or received an
      /// in-order packet for this request
      size_t progress_tsc;

      size_t cont_etid;  ///< eRPC thread ID to run the continuation on

      /// Pointers for the intrusive doubly-linked list of active RPCs
      SSlot *prev, *next;

      // Fields for congestion control, cold if CC is disabled.

      /// Packet number n is in the wheel (including its ready queue) iff
      /// in_wheel[n % kSessionCredits] is true
      std::array<bool, kSessionCredits> in_wheel;
      size_t wheel_count;  ///< Number of packets in the wheel (or ready queue)

      /// Per-packet TX timestamp. Indexed by pkt_num % kSessionCredits.
      std::array<size_t, kSessionCredits> tx_ts;
    } client_info;

    struct {
      /// The fake or dynamic request buffer
      MsgBuffer req_msgbuf;

      // Request metadata saved by the server before calling the request
      // handler. These fields are needed in enqueue_response(), and the request
      // MsgBuffer, which contains these fields, may not be valid at that point.

      /// The request type. This is set to a valid value only while we are
      /// waiting for an enqueue_response(), from a foreground or a background
      /// thread. This property is needed to safely reset sessions, and it is
      /// difficult to establish with other members (e.g., the MsgBuffers).
      uint8_t req_type;
      ReqFuncType req_func_type;  ///< The req handler type (e.g., background)

      /// Number of pkts received. Pkts up to (num_rx - 1) have been received.
      size_t num_rx;

      /// The server remembers the number of packets in the request after
      /// burying the request in enqueue_response().
      size_t sav_num_req_pkts;
    } server_info;
  };

  /// Return a string representation of the progress made by this sslot.
  /// Progress fields that are zero are not included in the string.
  std::string progress_str() const {
    std::ostringstream ret;
    if (is_client) {
      ret << "[num_tx " << client_info.num_tx << ", num_rx "
          << client_info.num_rx << "]";
    } else {
      ret << "[num_rx " << server_info.num_rx << "]";
    }
    return ret.str();
  }

 public:
  size_t get_cur_req_num() const { return cur_req_num; }
};

class ReqHandle : public SSlot {
 public:
  inline const MsgBuffer *get_req_msgbuf() const {
    return &server_info.req_msgbuf;
  }
};
}  // namespace erpc

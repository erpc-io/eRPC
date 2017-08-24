#ifndef ERPC_SSLOT_H
#define ERPC_SSLOT_H

#include "msg_buffer.h"
#include "ops.h"

namespace ERpc {

// Forward declarations for friendship
class IBTransport;
class Session;

template <typename T>
class Nexus;

template <typename T>
class Rpc;

/**
 * @brief Session slot metadata maintained about an RPC
 *
 * This slot structure is used by both server and client sessions.
 */
class SSlot {
  friend class Session;
  friend class Nexus<IBTransport>;
  friend class Rpc<IBTransport>;
  friend class ReqHandle;
  friend class RespHandle;

 public:
  SSlot() {}
  ~SSlot() {}

  // Server-only members. Exposed to req handlers, so not kept in server struct.
  MsgBuffer dyn_resp_msgbuf;  ///< Dynamic buffer to store RPC response
  MsgBuffer pre_resp_msgbuf;  ///< Preallocated buffer to store RPC response
  bool prealloc_used;         ///< Did the handler use \p pre_resp_msgbuf?

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

  size_t cur_req_num;

  union {
    // Info saved only at the client
    struct {
      MsgBuffer *resp_msgbuf;      ///< User-supplied response buffer
      erpc_cont_func_t cont_func;  ///< Continuation function for the request
      size_t tag;                  ///< Tag of the request

      size_t req_sent;      ///< Number of request packets sent
      size_t expl_cr_rcvd;  ///< Number of explicit credit returns received
      size_t rfr_sent;      ///< Number of request-for-response packets sent
      size_t resp_rcvd;     ///< Number of response packets received

      size_t enqueue_req_ts;  ///< Timestamp taken when request is enqueued
      size_t cont_etid;       ///< Thread ID to run the continuation on

      /// Return a string representation of the progress made by this sslot.
      /// Progress fields that are zero are not included in the string.
      std::string progress_str(size_t req_num) const {
        std::ostringstream ret;
        ret << "[req " << req_num << ",";
        if (req_sent != 0) ret << "[req_sent " << req_sent;
        if (expl_cr_rcvd != 0) ret << ", expl_cr_rcvd " << expl_cr_rcvd;
        if (rfr_sent != 0) ret << ", rfr_sent " << rfr_sent;
        if (resp_rcvd != 0) ret << ", resp_rcvd " << resp_rcvd;
        ret << "]";
        return ret.str();
      }

    } client_info;

    struct {
      /// The fake or dynamic request buffer. This is buried after the request
      /// handler returns, so it can be buried from a background thread.
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

      // RX progress. Note that the server does not track any TX progress
      size_t req_rcvd;  ///< Number of request packets received
      size_t rfr_rcvd;  ///< Number of request-for-response packets received
    } server_info;
  };
};

class ReqHandle : public SSlot {
 public:
  inline const MsgBuffer *get_req_msgbuf() const {
    return &server_info.req_msgbuf;
  }
};

class RespHandle : public SSlot {
 public:
  inline const MsgBuffer *get_resp_msgbuf() const {
    return client_info.resp_msgbuf;
  }
};
}

#endif  // ERPC_SSLOT_H

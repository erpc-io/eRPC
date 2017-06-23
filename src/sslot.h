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
class SSlot {
  friend class Session;
  friend class Nexus<IBTransport>;
  friend class Rpc<IBTransport>;
  friend class ReqHandle;
  friend class RespHandle;

 public:
  // Server-only members. These are exposed to request handlers.
  MsgBuffer dyn_resp_msgbuf;  ///< Dynamic MsgBuffer to store app response

  MsgBuffer pre_resp_msgbuf;  ///< Prealloc MsgBuffer to store app response
  bool prealloc_used;         ///< Did the app use \p pre_resp_msgbuf

 private:
  // Members that are valid for both server and client
  Session *session;      ///< Pointer to this sslot's session
  size_t index;          ///< Index of this sslot in the session's sslot_arr
  MsgBuffer *tx_msgbuf;  ///< The TX MsgBuffer, valid if it is not NULL
  MsgBuffer rx_msgbuf;   ///< The RX MsgBuffer, valid if \p buf is not NULL

  size_t cur_req_num;

  // Info saved only at the client
  struct {
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
    ///@{
    /// Request metadata saved by the server before calling the request handler.
    /// These fields are needed in enqueue_response(), and the req MsgBuffer,
    /// which can be used to infer these fields, may not be valid at that point.
    ///@}
    uint8_t req_type;
    ReqFuncType req_func_type;

    // Note that the server does not track any TX progress
    size_t req_rcvd;  ///< Number of request packets received
    size_t rfr_rcvd;  ///< Number of request-for-response packets received
  } server_info;

  /// Return a string representation of this session slot
  std::string to_string() const {
    if (rx_msgbuf.buf == nullptr && tx_msgbuf == nullptr) {
      return "[Invalid]";
    }

    // Sanity check: If the RX and TX MsgBuffers are both valid, they should
    // contain identical request number and type.
    if (rx_msgbuf.buf != nullptr && tx_msgbuf != nullptr) {
      assert(rx_msgbuf.get_req_num() == tx_msgbuf->get_req_num());
      assert(rx_msgbuf.get_req_type() == tx_msgbuf->get_req_type());
    }

    // Extract the request number and type from either RX or TX MsgBuffer
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

class ReqHandle : public SSlot {
 public:
  inline const MsgBuffer *get_req_msgbuf() const { return &rx_msgbuf; }
};

class RespHandle : public SSlot {
 public:
  inline const MsgBuffer *get_resp_msgbuf() const { return &rx_msgbuf; }
};
}

#endif  // ERPC_SSLOT_H

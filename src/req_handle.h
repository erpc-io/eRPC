#pragma once
#include "session.h"

namespace erpc {

/// Handle object passed by eRPC to the application's request handler callbacks
class ReqHandle : public SSlot {
 public:
  /// Get this RPC's request message buffer. Valid until the request handler
  /// enqueues a response.
  inline const MsgBuffer *get_req_msgbuf() const {
    return &server_info_.req_msgbuf_;
  }

  /// Get the RPC ID of the server-side RPC object that received this request
  /// from the network. Valid until the request handler enqueues a response.
  inline uint8_t get_server_rpc_id() const {
    return session_->server_.rpc_id_;
  }

  /// Get the server-side session number for this request. Valid until the
  /// request handler enqueues a response.
  inline uint16_t get_server_session_num() const {
    return session_->server_.session_num_;
  }
};
}  // namespace erpc

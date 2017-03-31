/**
 * @file ops.h
 * @brief User-defined eRPC request and response handler types
 */

#ifndef ERPC_OPS_H
#define ERPC_OPS_H

#include "msg_buffer.h"

namespace ERpc {

class SSlot; /* Forward declaration */
typedef SSlot ReqHandle;
typedef SSlot RespHandle;

/// The application-defined request handler
typedef void (*erpc_req_func_t)(ReqHandle *req_handle,
                                const MsgBuffer *req_msgbuf, void *context);

/// The continuation function
typedef void (*erpc_cont_func_t)(RespHandle *resp_handle,
                                 const MsgBuffer *resp_msgbuf, void *context,
                                 size_t tag);

enum class ReqFuncType : uint8_t { kFgTerminal, kFgNonterminal, kBackground };

/// The application-specified eRPC request and response handlers. An \p Ops
/// object is invalid if either of the two function pointers are NULL.
class ReqFunc {
 public:
  erpc_req_func_t req_func;
  ReqFuncType req_func_type;

  inline bool is_fg_terminal() const {
    return req_func_type == ReqFuncType::kFgTerminal;
  }

  inline bool is_background() const {
    return req_func_type == ReqFuncType::kBackground;
  }

  ReqFunc() { req_func = nullptr; }

  ReqFunc(erpc_req_func_t req_func, ReqFuncType req_func_type)
      : req_func(req_func), req_func_type(req_func_type) {
    if (req_func == nullptr) {
      throw std::runtime_error("Invalid Ops with NULL handler function");
    }
  }

  /// Check if handlers have been registered for this Ops
  inline bool is_registered() const { return req_func != nullptr; }
};
}

#endif  // ERPC_OPS_H

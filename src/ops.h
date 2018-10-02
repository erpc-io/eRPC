/**
 * @file ops.h
 * @brief User-defined eRPC request handler and continuation types
 */
#pragma once

#include "msg_buffer.h"

namespace erpc {

// Forward declarations
class SSlot;
class ReqHandle;
class RespHandle;

/// The request handler
typedef void (*erpc_req_func_t)(ReqHandle *req_handle, void *context);

/// The continuation function
typedef void (*erpc_cont_func_t)(RespHandle *resp_handle, void *context,
                                 size_t tag);

/// The request handler types
enum class ReqFuncType : uint8_t {
  kForeground,  ///< Request handler runs in foreground (network I/O) thread
  kBackground   ///< Request handler runs in a background thread
};

/// The application-specified eRPC request handler
class ReqFunc {
 public:
  erpc_req_func_t req_func;
  ReqFuncType req_func_type;

  inline bool is_background() const {
    return req_func_type == ReqFuncType::kBackground;
  }

  ReqFunc() { req_func = nullptr; }

  ReqFunc(erpc_req_func_t req_func, ReqFuncType req_func_type)
      : req_func(req_func), req_func_type(req_func_type) {
    rt_assert(req_func != nullptr, "Invalid Ops with null handler function");
  }

  /// Check if this request handler is registered
  inline bool is_registered() const { return req_func != nullptr; }
};

/// The arguments to enqueue_request()
struct enq_req_args_t {
  int session_num;
  uint8_t req_type;
  MsgBuffer *req_msgbuf;
  MsgBuffer *resp_msgbuf;
  erpc_cont_func_t cont_func;
  size_t tag;
  size_t cont_etid;

  enq_req_args_t() {}
  enq_req_args_t(int session_num, uint8_t req_type, MsgBuffer *req_msgbuf,
                 MsgBuffer *resp_msgbuf, erpc_cont_func_t cont_func, size_t tag,
                 size_t cont_etid)
      : session_num(session_num),
        req_type(req_type),
        req_msgbuf(req_msgbuf),
        resp_msgbuf(resp_msgbuf),
        cont_func(cont_func),
        tag(tag),
        cont_etid(cont_etid) {}
};
}  // namespace erpc

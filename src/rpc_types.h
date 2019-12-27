/**
 * @file rpc_types.h
 * @brief Types exposed to the eRPC user
 */
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <type_traits>
#include "common.h"

namespace erpc {

// Forward declarations
class SSlot;
class MsgBuffer;
class ReqHandle;

/**
 * @relates Rpc
 *
 * @brief The type of the request handler function invoked at the server on
 * receiving a request.
 *
 * The application need not enqueue the response inside the request handler.
 * It can do so later.
 *
 * Request buffer ownership: The application owns the request message buffer
 * until it enqueues the response, except in one common case. If zero-copy
 * RX is enabled (i.e., kZeroCopyRx is true) and the request message fits in
 * one packet, the application owns the request message buffer for only the
 * duration of the request handler.
 *
 * @param ReqHandle A handle to the received request
 * @param context The context that was used while creating the Rpc object
 */
typedef void (*erpc_req_func_t)(ReqHandle *req_handle, void *context);

/**
 * @relates Rpc
 *
 * @brief The type of the continuation callback invoked at the client. This
 * returns ownership of the request and response message buffers that the
 * application supplied in Rpc::enqueue_request back to the application.
 *
 * @param context The context that was used while creating the Rpc object
 * @param tag The tag used by the application for this request
 */
typedef void (*erpc_cont_func_t)(void *context, void *tag);

/**
 * @relates Rpc
 * @brief The possible kinds of request handlers. Foreground-mode handlers run
 * in the thread that calls the event loop. Background-mode handlers run in
 * background threads spawned by eRPC.
 */
enum class ReqFuncType : uint8_t { kForeground, kBackground };

/**
 * @relates Rpc
 * @brief The request handler registered by applications
 */
class ReqFunc {
 public:
  erpc_req_func_t req_func;   ///< The handler function
  ReqFuncType req_func_type;  ///< The handlers's mode (foreground/background)

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
}  // namespace erpc

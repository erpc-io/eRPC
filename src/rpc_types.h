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
 * @brief The type of the request handler function invoked at the server when a
 * request is received. The application owns the request handle (and therefore
 * the request message buffer) until it calls Rpc::enqueue_response.
 *
 * The application need not enqueue the response in the body of the request
 * handler. This is true even if the request handler is foreground-mode.
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

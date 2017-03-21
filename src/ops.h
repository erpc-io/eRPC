/**
 * @file ops.h
 * @brief User-defined eRPC request and response handler types
 */

#ifndef ERPC_RPC_TYPES_H
#define ERPC_RPC_TYPES_H

#include "msg_buffer.h"

namespace ERpc {

/// A structure to hold the application's response
struct app_resp_t {
  bool prealloc_used;         ///< Did the app use pre_resp_msgbuf?
  MsgBuffer pre_resp_msgbuf;  ///< Preallocated storage for small responses
  MsgBuffer dyn_resp_msgbuf;  ///< Dynamic storage for large responses
};

/// The application-defined request handler
typedef void (*erpc_req_handler_t)(const MsgBuffer *req_msgbuf,
                                   app_resp_t *app_resp, void *context);

/// The application-defined response handler
typedef void (*erpc_resp_handler_t)(const MsgBuffer *req_msgbuf,
                                    const MsgBuffer *resp_msgbuf,
                                    void *context);

/// The application-specified eRPC request and response handlers. An \p Ops
/// object is invalid if either of the two function pointers are NULL.
class Ops {
 public:
  erpc_req_handler_t req_handler;
  erpc_resp_handler_t resp_handler;
  bool run_in_background;  ///< Should request handler run in the background?

  Ops() {
    run_in_background = false;
    req_handler = nullptr;
    resp_handler = nullptr;
  }

  Ops(erpc_req_handler_t req_handler, erpc_resp_handler_t resp_handler,
      bool run_in_background = false)
      : req_handler(req_handler),
        resp_handler(resp_handler),
        run_in_background(run_in_background) {
    if (req_handler == nullptr || resp_handler == nullptr) {
      throw std::runtime_error("Invalid Ops with NULL handler function");
    }
  }

  inline bool is_valid() const {
    return req_handler != nullptr; /* Checking one is good enough */
  }
};
}

#endif  // ERPC_RPC_TYPES_H

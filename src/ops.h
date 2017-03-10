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
  MsgBuffer *resp_msgbuf;     ///< Storage for large responses
  size_t resp_size;  ///< The number of data bytes in the app's response
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
  erpc_req_handler_t erpc_req_handler = nullptr;
  erpc_resp_handler_t erpc_resp_handler = nullptr;
};
}

#endif  // ERPC_RPC_TYPES_H

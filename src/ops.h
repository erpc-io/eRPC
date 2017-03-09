/**
 * @file ops.h
 * @brief User-defined eRPC request and response handler types
 */

#ifndef ERPC_RPC_TYPES_H
#define ERPC_RPC_TYPES_H

#include "msg_buffer.h"
#include "util/buffer.h"

namespace ERpc {
struct app_resp_t {
  bool prealloc_used;
  Buffer *prealloc_resp_buffer;
  Buffer *resp_buffer;
  size_t resp_size;
};

typedef void (*erpc_req_handler_t)(const MsgBuffer *req_msgbuf,
                                   app_resp_t *app_resp);

typedef void (*erpc_resp_handler_t)(const MsgBuffer *resp_msgbuf);

/// The application-specified eRPC request and response handlers. An \p Ops
/// object is invalid if either of the two function pointers are NULL.
class Ops {
 public:
  erpc_req_handler_t erpc_req_handler = nullptr;
  erpc_req_handler_t erpc_resp_handler = nullptr;
};
}

#endif  // ERPC_RPC_TYPES_H

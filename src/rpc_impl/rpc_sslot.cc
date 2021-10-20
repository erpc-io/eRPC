#include "rpc.h"

namespace erpc {

uint8_t ReqHandle::get_server_rpc_id() const {
  return session_->server_.rpc_id_;
}

uint16_t ReqHandle::get_server_session_num() const {
  return session_->server_.session_num_;
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

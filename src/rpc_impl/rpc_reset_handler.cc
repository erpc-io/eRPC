/**
 * @file rpc_reset_handler.cc
 * @brief Handler for ENet reset event
 */
#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::handle_reset_st(const std::string rem_hostname) {
  assert(in_creator());
  _unused(rem_hostname);
  LOG_INFO("eRPC Rpc %u: Received reset event for remote hostname = %s.\n",
           rpc_id, rem_hostname.c_str());
}

}  // End ERpc

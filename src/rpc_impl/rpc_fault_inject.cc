/*
 * @file rpc_fault_inject.cc
 * @brief Functions to allow users to inject faults into ERpc
 */
#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::check_fault_injection_enabled() {
  if (!kFaultInjection) {
    throw std::runtime_error("eRPC Rpc: Fault injection is disabled.");
  }
}

template <class TTr>
void Rpc<TTr>::fault_inject_resolve_server_rinfo() {
  check_fault_injection_enabled();
  faults.resolve_server_rinfo = true;
}

}  // End ERpc

/*
 * @file rpc_fault_inject.cc
 * @brief Functions to allow users to inject faults into eRPC
 */
#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::fault_inject_check_ok() const {
  if (!kFaultInjection) throw std::runtime_error("eRPC Rpc: Faults disabled.");
  rt_assert(in_dispatch(),
            "eRPC Rpc: Non-creator threads can't inject faults.");
}

template <class TTr>
void Rpc<TTr>::fault_inject_fail_resolve_server_rinfo_st() {
  fault_inject_check_ok();
  faults.fail_resolve_server_rinfo = true;
}

template <class TTr>
void Rpc<TTr>::fault_inject_set_pkt_drop_prob_st(double p) {
  fault_inject_check_ok();
  assert(p == 0.0 || (p >= 1.0 / 1000000000 && p < .95));
  faults.pkt_drop_prob = p;
  faults.pkt_drop_thresh_billion = p * 1000000000;
}

}  // End erpc

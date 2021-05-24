/*
 * @file rpc_fault_inject.cc
 * @brief Functions to allow users to inject faults into eRPC
 */
#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::fault_inject_check_ok() const {
  rt_assert(kTesting, "Faults disabled");
  rt_assert(in_dispatch(), "Non-creator threads can't inject faults.");
}

template <class TTr>
void Rpc<TTr>::fault_inject_fail_resolve_rinfo_st() {
  fault_inject_check_ok();
  faults_.fail_resolve_rinfo_ = true;
}

template <class TTr>
void Rpc<TTr>::fault_inject_set_pkt_drop_prob_st(double p) {
  fault_inject_check_ok();
  assert(p == 0.0 || (p >= 1.0 / 1000000000 && p < .95));
  faults_.pkt_drop_prob_ = p;
  faults_.pkt_drop_thresh_billion_ = p * 1000000000;
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

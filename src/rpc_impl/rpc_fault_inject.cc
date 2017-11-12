/*
 * @file rpc_fault_inject.cc
 * @brief Functions to allow users to inject faults into eRPC
 */
#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::fault_inject_check_ok() const {
  if (!kFaultInjection) throw std::runtime_error("eRPC Rpc: Faults disabled.");
  rt_assert(in_creator(), "eRPC Rpc: Non-creator threads can't inject faults.");
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

template <class TTr>
void Rpc<TTr>::fault_inject_reset_remote_epeer_st(int session_num) {
  fault_inject_check_ok();
  assert(is_usr_session_num_in_range_st(session_num));

  // We don't grab session lock because, for this session, other management ops
  // are handled by this thread, and we don't care about datapath operations
  Session *session = session_vec[static_cast<size_t>(session_num)];
  assert(session != nullptr);
  assert(session->is_client() && session->is_connected());

  LOG_WARN(
      "eRPC Rpc %u: Sending reset-remote-peer fault for session %u "
      "to [%s, %u].\n",
      rpc_id, session->local_session_num, session->server.hostname,
      session->server.rpc_id);

  enqueue_sm_req_st(session, SmPktType::kFaultResetPeerReq);
}

}  // End erpc

/*
 * @file rpc_fault_inject.cc
 * @brief Functions to allow users to inject faults into ERpc
 */
#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::fault_inject_check_ok() const {
  if (!kFaultInjection) {
    throw std::runtime_error("eRPC Rpc: Fault injection is disabled.");
  }

  if (!in_creator()) {
    throw std::runtime_error(
        "eRPC Rpc: Non-creator threads cannot inject faults.");
  }
}

template <class TTr>
void Rpc<TTr>::fault_inject_fail_resolve_server_rinfo_st() {
  fault_inject_check_ok();
  faults.fail_resolve_server_rinfo = true;
}

template <class TTr>
void Rpc<TTr>::fault_inject_set_pkt_drop_prob_st(double pkt_drop_prob) {
  fault_inject_check_ok();
  assert(pkt_drop_prob >= 0.0 && pkt_drop_prob < .95);
  faults.pkt_drop_prob = pkt_drop_prob;
}

template <class TTr>
void Rpc<TTr>::fault_inject_disable_pkt_loss_handling() {
  fault_inject_check_ok();
  faults.disable_pkt_loss_handling = true;
}

template <class TTr>
void Rpc<TTr>::fault_inject_reset_remote_epeer_st(int session_num) {
  fault_inject_check_ok();
  assert(is_usr_session_num_in_range(session_num));

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

}  // End ERpc

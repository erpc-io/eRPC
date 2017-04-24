/*
 * @file rpc_fault_inject.cc
 * @brief Functions to allow users to inject faults into ERpc
 */
#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::check_fault_injection_ok() const {
  if (!kFaultInjection) {
    throw std::runtime_error("eRPC Rpc: Fault injection is disabled.");
  }

  if (!in_creator()) {
    throw std::runtime_error(
        "eRPC Rpc: Non-creator threads cannot inject faults.");
  }
}

template <class TTr>
void Rpc<TTr>::fault_inject_resolve_server_rinfo_st() {
  check_fault_injection_ok();
  faults.resolve_server_rinfo = true;
}

template <class TTr>
void Rpc<TTr>::fault_inject_drop_tx_local() {
  check_fault_injection_ok();
  faults.drop_tx_local = true;
}

template <class TTr>
void Rpc<TTr>::fault_inject_drop_tx_remote(int session_num) {
  check_fault_injection_ok();

  if (!is_usr_session_num_in_range(session_num)) {
    throw std::runtime_error("eRPC Rpc: Invalid session number.");
  }

  Session *session = session_vec[static_cast<size_t>(session_num)];
  if (session == nullptr) {
    throw std::runtime_error("eRPC Rpc: Session already destroyed.");
  }

  // Lock the session to prevent concurrent request submission
  lock_cond(&session->lock);

  if (!session->is_client()) {
    unlock_cond(&session->lock);
    throw std::runtime_error("eRPC Rpc: Session is not a client session.");
  }

  erpc_dprintf(
      "eRPC Rpc %u: Sending drop TX remote fault for session %u to [%s, %u].\n",
      rpc_id, session->local_session_num, session->server.hostname,
      session->server.rpc_id);

  // Enqueue a session management work request
  enqueue_sm_req(session, SessionMgmtPktType::kFaultDropTxRemote);

  unlock_cond(&session->lock);
}

template <class TTr>
void Rpc<TTr>::fault_inject_reset_remote_epeer_st(int session_num) {
  check_fault_injection_ok();

  if (!is_usr_session_num_in_range(session_num)) {
    throw std::runtime_error("eRPC Rpc: Invalid session number.");
  }

  Session *session = session_vec[static_cast<size_t>(session_num)];
  if (session == nullptr) {
    throw std::runtime_error("eRPC Rpc: Session already destroyed.");
  }

  // Lock the session to prevent concurrent request submission
  lock_cond(&session->lock);

  if (!session->is_client()) {
    unlock_cond(&session->lock);
    throw std::runtime_error("eRPC Rpc: Session is not a client session.");
  }

  erpc_dprintf(
      "eRPC Rpc %u: Sending reset remote peer fault for session %u "
      "to [%s, %u].\n",
      rpc_id, session->local_session_num, session->server.hostname,
      session->server.rpc_id);

  // Enqueue a session management work request
  enqueue_sm_req(session, SessionMgmtPktType::kFaultResetPeerReq);

  unlock_cond(&session->lock);
}

}  // End ERpc

/**
 * @file rpc_sm_retry.cc
 * @brief Methods to send/resend session management requests.
 */

#include <algorithm>

#include "rpc.h"
#include "util/udp_client.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::send_connect_req_one_st(Session *session) {
  assert(in_creator());
  assert(session != nullptr && session->is_client());
  assert(session->state == SessionState::kConnectInProgress);

  SessionMgmtPkt connect_req(SessionMgmtPktType::kConnectReq);
  connect_req.client = session->client;
  connect_req.server = session->server;
  connect_req.send_to(session->server.hostname, &nexus->udp_config);
}

template <class TTr>
void Rpc<TTr>::send_disconnect_req_one_st(Session *session) {
  assert(in_creator());
  assert(session != nullptr && session->is_client());
  assert(session->state == SessionState::kDisconnectInProgress);

  SessionMgmtPkt connect_req(SessionMgmtPktType::kDisconnectReq);
  connect_req.client = session->client;
  connect_req.server = session->server;
  connect_req.send_to(session->server.hostname, &nexus->udp_config);
}

template <class TTr>
bool Rpc<TTr>::mgmt_retryq_contains_st(Session *session) {
  assert(in_creator());
  return std::find(mgmt_retry_queue.begin(), mgmt_retry_queue.end(), session) !=
         mgmt_retry_queue.end();
}

template <class TTr>
void Rpc<TTr>::mgmt_retryq_add_st(Session *session) {
  assert(in_creator());
  assert(session != nullptr && session->is_client());

  /* Only client-mode sessions can be in the management retry queue */
  assert(session->is_client());

  /* Ensure that we don't have an in-flight management req for this session */
  assert(!mgmt_retryq_contains_st(session));

  session->client_info.mgmt_req_tsc = rdtsc(); /* Save tsc for retry */
  mgmt_retry_queue.push_back(session);
}

template <class TTr>
void Rpc<TTr>::mgmt_retryq_remove_st(Session *session) {
  assert(in_creator());
  assert(session != nullptr && session->is_client());
  assert(mgmt_retryq_contains_st(session));

  size_t initial_size = mgmt_retry_queue.size(); /* Debug-only */
  _unused(initial_size);

  mgmt_retry_queue.erase(
      std::remove(mgmt_retry_queue.begin(), mgmt_retry_queue.end(), session),
      mgmt_retry_queue.end());

  assert(mgmt_retry_queue.size() == initial_size - 1);
}

template <class TTr>
void Rpc<TTr>::mgmt_retry() {
  assert(in_creator());
  assert(mgmt_retry_queue.size() > 0);
  uint64_t cur_tsc = rdtsc();

  for (Session *session : mgmt_retry_queue) {
    assert(session != nullptr);
    SessionState state = session->state;
    assert(state == SessionState::kConnectInProgress ||
           state == SessionState::kDisconnectInProgress);

    uint64_t elapsed_cycles = cur_tsc - session->client_info.mgmt_req_tsc;
    assert(elapsed_cycles > 0);

    double elapsed_ms = to_sec(elapsed_cycles, nexus->freq_ghz) * 1000;
    if (elapsed_ms > kSessionMgmtRetransMs) {
      /* We need to retransmit */
      switch (state) {
        case SessionState::kConnectInProgress:
          erpc_dprintf(
              "eRPC Rpc %u: Retrying connect req for session %u to [%s, %u].\n",
              rpc_id, session->client.session_num, session->server.hostname,
              session->server.rpc_id);

          send_connect_req_one_st(session);
          break; /* Process other in-flight requests */
        case SessionState::kDisconnectInProgress:
          erpc_dprintf(
              "eRPC Rpc %u: Retrying disconnect req for session %u to "
              "[%s, %u].\n",
              rpc_id, session->client.session_num, session->server.hostname,
              session->server.rpc_id);

          send_disconnect_req_one_st(session);
          break; /* Process other in-flight requests */
        default:
          assert(false);
          exit(-1);
      }

      session->client_info.mgmt_req_tsc = rdtsc(); /* Update for retry */
    }
  }
}

}  // End ERpc

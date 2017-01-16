/**
 * @file rpc_sm_retry.cc
 * @brief Methods to send/resend session management requests.
 */

#include <algorithm>

#include "rpc.h"
#include "util/udp_client.h"

namespace ERpc {

/**
 * @brief Send or resend the connect request for a session
 */
template <class Transport_>
void Rpc<Transport_>::send_connect_req_one(Session *session) {
  assert(is_session_ptr_client(session));
  assert(session->state == SessionState::kConnectInProgress);

  SessionMgmtPkt connect_req(SessionMgmtPktType::kConnectReq);
  connect_req.client = session->client;
  connect_req.server = session->server;
  connect_req.send_to(session->server.hostname, &nexus->udp_config);
}

/**
 * @brief Send or resend the disconnect request for a session
 */
template <class Transport_>
void Rpc<Transport_>::send_disconnect_req_one(Session *session) {
  assert(is_session_ptr_client(session));
  assert(session->state == SessionState::kDisconnectInProgress);

  SessionMgmtPkt connect_req(SessionMgmtPktType::kDisconnectReq);
  connect_req.client = session->client;
  connect_req.server = session->server;
  connect_req.send_to(session->server.hostname, &nexus->udp_config);
}

template <class Transport_>
bool Rpc<Transport_>::is_in_flight(Session *session) {
  for (in_flight_req_t &req : in_flight_vec) {
    if (req.session == session) {
      return true;
    }
  }

  return false;
}

template <class Transport_>
void Rpc<Transport_>::add_to_in_flight(Session *session) {
  assert(is_session_ptr_client(session));

  /* Only client-mode sessions can have requests in flight */
  assert(session->role == Session::Role::kClient);

  /* Ensure that we don't have this session in flight already */
  assert(!is_in_flight(session));

  uint64_t tsc = rdtsc();
  in_flight_vec.push_back(in_flight_req_t(tsc, session));
}

template <class Transport_>
void Rpc<Transport_>::remove_from_in_flight(Session *session) {
  assert(is_session_ptr_client(session));
  assert(is_in_flight(session));

  size_t initial_size = in_flight_vec.size();

  in_flight_req_t dummy_req(0, session); /* Dummy for std::remove */
  in_flight_vec.erase(
      std::remove(in_flight_vec.begin(), in_flight_vec.end(), dummy_req),
      in_flight_vec.end());

  assert(in_flight_vec.size() == initial_size - 1);
}

template <class Transport_>
void Rpc<Transport_>::retry_in_flight() {
  assert(in_flight_vec.size() > 0);
  uint64_t cur_tsc = rdtsc();

  for (in_flight_req_t &req : in_flight_vec) {
    assert(req.session != nullptr);
    SessionState state = req.session->state;
    assert(state == SessionState::kConnectInProgress ||
           state == SessionState::kDisconnectInProgress);

    uint64_t elapsed_cycles = cur_tsc - req.prev_tsc;
    assert(elapsed_cycles > 0);

    double elapsed_ms = to_sec(elapsed_cycles, nexus->freq_ghz) * 1000;
    if (elapsed_ms > kSessionMgmtRetransMs) {
      /* We need to retransmit */
      switch (state) {
        case SessionState::kConnectInProgress:
          send_connect_req_one(req.session);
          break; /* Process other in-flight requests */
        case SessionState::kDisconnectInProgress:
          send_disconnect_req_one(req.session);
          break; /* Process other in-flight requests */
        default:
          assert(false);
      }

      /* Update the timestamp of the in-flight request. (@req is a reference) */
      req.prev_tsc = cur_tsc;
    }
  }
}

}  // End ERpc

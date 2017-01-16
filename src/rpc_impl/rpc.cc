#include <algorithm>

#include "rpc.h"
#include "util/udp_client.h"

namespace ERpc {

template <class Transport_>
Rpc<Transport_>::Rpc(Nexus *nexus, void *context, size_t app_tid,
                     session_mgmt_handler_t session_mgmt_handler,
                     std::vector<size_t> fdev_port_vec)
    : nexus(nexus),
      context(context),
      app_tid(app_tid),
      session_mgmt_handler(session_mgmt_handler),
      num_fdev_ports(fdev_port_vec.size()) {
  if (fdev_port_vec.size() == 0) {
    fprintf(stderr, "eRPC Rpc: FATAL. Rpc created with 0 fabric ports.\n");
    exit(-1);
  }

  if (fdev_port_vec.size() > kMaxFabDevPorts) {
    fprintf(stderr, "eRPC Rpc: FATAL. Only %zu local ports supported.\n",
            kMaxFabDevPorts);
    exit(-1);
  }

  /* Record the requested local ports in an array */
  int i = 0;
  for (size_t fdev_port : fdev_port_vec) {
    fdev_port_arr[i] = fdev_port;
    i++;
  }

  /* Initialize the transport */
  transport = new Transport_();

  /* Register a hook with the Nexus */
  sm_hook.app_tid = app_tid;
  nexus->register_hook((SessionMgmtHook *)&sm_hook);
}

template <class Transport_>
Rpc<Transport_>::~Rpc() {
  for (Session *session : session_vec) {
    if (session != nullptr) {
      /* Free this session */
      _unused(session);
    }
  }
}

template <class Transport_>
uint64_t Rpc<Transport_>::generate_start_seq() {
  uint64_t rand = slow_rand.next_u64();
  return (rand & kStartSeqMask);
}

template <class Transport_>
bool Rpc<Transport_>::is_session_managed(Session *session) {
  assert(session != NULL);

  if (std::find(session_vec.begin(), session_vec.end(), session) ==
      session_vec.end()) {
    return false;
  }

  return true;
}

/**
 * @brief Send or resend the connect request for a session
 */
template <class Transport_>
void Rpc<Transport_>::send_connect_req_one(Session *session) {
  assert(session != NULL);
  assert(is_session_managed(session));
  assert(session->role == Session::Role::kClient);

  /*
   * We may send/resend the connect request packet in two cases:
   * 1. After create_session() in the kConnectInProgress state.
   * 2. If the user calls destroy session (which moves the session to
   *    kDisconnectWaitForConnect) before the connection is established.
   */
  assert(session->state == SessionState::kConnectInProgress ||
         session->state == SessionState::kDisconnectWaitForConnect);

  SessionMgmtPkt connect_req(SessionMgmtPktType::kConnectReq);
  memcpy((void *)&connect_req.client, (void *)&session->client,
         sizeof(connect_req.client));
  memcpy((void *)&connect_req.server, (void *)&session->server,
         sizeof(connect_req.server));

  connect_req.send_to(session->server.hostname, &nexus->udp_config);
}

/**
 * @brief Send or resend the disconnect request for a session
 */
template <class Transport_>
void Rpc<Transport_>::send_disconnect_req_one(Session *session) {
  _unused(session);
}

template <class Transport_>
void Rpc<Transport_>::add_to_in_flight(Session *session) {
  assert(session != nullptr);
  assert(is_session_managed(session));

  /* Only client-mode sessions can have requests in flight */
  assert(session->role == Session::Role::kClient);

  /* Ensure that we don't have this session in flight already */
  for (in_flight_req_t &req : in_flight_vec) {
    if (req.session == session) {
      assert(false);
    }
  }

  uint64_t tsc = rdtsc();
  in_flight_vec.push_back(in_flight_req_t(tsc, session));
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
void Rpc<Transport_>::remove_from_in_flight(Session *session) {
  assert(session != nullptr);
  assert(is_session_managed(session));
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
           state == SessionState::kDisconnectWaitForConnect ||
           state == SessionState::kDisconnectInProgress);

    uint64_t elapsed_cycles = cur_tsc - req.prev_tsc;
    assert(elapsed_cycles > 0);

    double elapsed_ms = to_sec(elapsed_cycles, nexus->freq_ghz) * 1000;
    if (elapsed_ms > kSessionMgmtRetransMs) {
      /* We need to retransmit */
      switch (state) {
        case SessionState::kConnectInProgress:
        case SessionState::kDisconnectWaitForConnect:
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

template <class Transport_>
std::string Rpc<Transport_>::get_name() {
  std::string ret;
  ret += std::string("[");
  ret += std::string(nexus->hostname);
  ret += std::string(", ");
  ret += std::to_string(app_tid);
  ret += std::string("]");
  return ret;
}

template <class Transport_>
bool Rpc<Transport_>::is_fdev_port_managed(size_t fab_port_index) {
  for (size_t i = 0; i < num_fdev_ports; i++) {
    if (fdev_port_arr[i] == fab_port_index) {
      return true;
    }
  }
  return false;
}

template <class Transport_>
void Rpc<Transport_>::send_request(const Session *session,
                                   const Buffer *buffer) {
  _unused(session);
  _unused(buffer);
}

template <class Transport_>
void Rpc<Transport_>::send_response(const Session *session,
                                    const Buffer *buffer) {
  _unused(session);
  _unused(buffer);
};

}  // End ERpc

/**
 * @file rpc.cc
 * @brief Simple Rpc-related methods.
 */

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

template <class Transport_>
void Rpc<Transport_>::handle_session_management() {
  assert(sm_hook.session_mgmt_ev_counter > 0);
  sm_hook.session_mgmt_mutex.lock();

  /* Handle all session management requests */
  for (SessionMgmtPkt *sm_pkt : sm_hook.session_mgmt_pkt_list) {
    /* The sender of a packet cannot be this Rpc */
    if (session_mgmt_is_pkt_type_req(sm_pkt->pkt_type)) {
      assert(!(strcmp(sm_pkt->client.hostname, nexus->hostname) == 0 &&
               sm_pkt->client.app_tid == app_tid));
    } else {
      assert(!(strcmp(sm_pkt->server.hostname, nexus->hostname) == 0 &&
               sm_pkt->server.app_tid == app_tid));
    }

    switch (sm_pkt->pkt_type) {
      case SessionMgmtPktType::kConnectReq:
        handle_session_connect_req(sm_pkt);
        break;
      case SessionMgmtPktType::kConnectResp:
        handle_session_connect_resp(sm_pkt);
        break;
      case SessionMgmtPktType::kDisconnectReq:
        handle_session_connect_resp(sm_pkt);
        break;
      case SessionMgmtPktType::kDisconnectResp:
        handle_session_connect_resp(sm_pkt);
        break;
      default:
        assert(false);
        break;
    }

    /* Free memory that was allocated by the Nexus */
    free(sm_pkt);
  }

  sm_hook.session_mgmt_pkt_list.clear();
  sm_hook.session_mgmt_ev_counter = 0;
  sm_hook.session_mgmt_mutex.unlock();
};

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

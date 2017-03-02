/**
 * @file rpc.cc
 * @brief Simple Rpc-related methods.
 */

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "rpc.h"
#include "util/udp_client.h"

namespace ERpc {

template <class Transport_>
Rpc<Transport_>::Rpc(Nexus *nexus, void *context, uint8_t app_tid,
                     session_mgmt_handler_t session_mgmt_handler,
                     uint8_t phy_port, size_t numa_node)
    : nexus(nexus),
      context(context),
      app_tid(app_tid),
      session_mgmt_handler(session_mgmt_handler),
      phy_port(phy_port),
      numa_node(numa_node) {
  /* Ensure that we're running as root */
  if (getuid()) {
    throw std::runtime_error("eRPC Rpc: You need to be root to use eRPC");
    return;
  }

  if (nexus == nullptr) {
    throw std::invalid_argument("eRPC Rpc: Invalid nexus");
    return;
  }

  if (app_tid == kInvalidAppTid || nexus->app_tid_exists(app_tid)) {
    throw std::invalid_argument("eRPC Rpc: Invalid app_tid");
    return;
  }

  if (phy_port >= kMaxPhyPorts) {
    throw std::invalid_argument("eRPC Rpc: Invalid physical port");
    return;
  }

  if (numa_node >= kMaxNumaNodes) {
    throw std::invalid_argument("eRPC Rpc: Invalid NUMA node");
    return;
  }

  /*
   * Partially initialize the transport without using hugepages. This
   * initializes the transport's memory registration functions required for
   * the hugepage allocator.
   */
  transport = new Transport_(app_tid, phy_port);

  huge_alloc =
      new HugeAllocator(kInitialHugeAllocSize, numa_node,
                        transport->reg_mr_func, transport->dereg_mr_func);

  try {
    /* Complete transport initialization using the hugepage allocator */
    transport->init_hugepage_structures(huge_alloc);
  } catch (std::runtime_error e) {
    /* Free any huge pages that \p transport might have created */
    delete huge_alloc;
    throw e;
  }

  /* Register a hook with the Nexus */
  sm_hook.app_tid = app_tid;
  nexus->register_hook((SessionMgmtHook *)&sm_hook);
}

template <class Transport_>
Rpc<Transport_>::~Rpc() {
  /*
   * First delete the hugepage allocator. This deregisters and deletes the
   * SHM regions. Deregistration is done using \p transport's deregistration
   * function, so \p transport is deleted later.
   */
  delete huge_alloc;

  /* Allow \p transport to clean up non-hugepage structures */
  delete transport;

  for (Session *session : session_vec) {
    if (session != nullptr) {
      /* Free this session */
      _unused(session);
    }
  }
}

template <class Transport_>
void Rpc<Transport_>::bury_session(Session *session) {
  assert(session != nullptr);

  /* First, free session resources */
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    /* Free the preallocated MsgBuffer */
    assert(session->msg_arr[i]._prealloc.is_valid());
    huge_alloc->free_buf(session->msg_arr[i]._prealloc);

    /* XXX: Who frees msg_arr[i].msg_buffer? */
  }

  /* Second, mark the session as NULL in the session vector */
  uint16_t session_num;
  if (session->role == Session::Role::kClient) {
    assert(!mgmt_retry_queue_contains(session));

    session_num = session->client.session_num;
  } else {
    session_num = session->server.session_num;
  }

  session_vec.at(session_num) = nullptr;
  delete session;
}

template <class Transport_>
void Rpc<Transport_>::handle_session_management() {
  assert(sm_hook.session_mgmt_ev_counter > 0);
  sm_hook.session_mgmt_mutex.lock();

  /* Handle all session management requests */
  for (SessionMgmtPkt *sm_pkt : sm_hook.session_mgmt_pkt_list) {
    /* The sender of a packet cannot be this Rpc */
    if (session_mgmt_pkt_type_is_req(sm_pkt->pkt_type)) {
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
        handle_session_disconnect_req(sm_pkt);
        break;
      case SessionMgmtPktType::kDisconnectResp:
        handle_session_disconnect_resp(sm_pkt);
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
}

template <class Transport_>
std::string Rpc<Transport_>::get_name() {
  std::ostringstream ret;
  ret << "[" << trim_hostname(std::string(nexus->hostname)) << ", "
      << std::to_string(app_tid) << "]";
  return ret.str();
}

}  // End ERpc

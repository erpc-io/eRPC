/**
 * @file rpc.cc
 * @brief Simple Rpc-related methods.
 */

#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "rpc.h"
#include "util/udp_client.h"

namespace ERpc {

template <class TTr>
Rpc<TTr>::Rpc(Nexus *nexus, void *context, uint8_t app_tid,
              session_mgmt_handler_t session_mgmt_handler, uint8_t phy_port,
              size_t numa_node)
    : nexus(nexus),
      context(context),
      app_tid(app_tid),
      session_mgmt_handler(session_mgmt_handler),
      phy_port(phy_port),
      numa_node(numa_node),
      ops_arr(nexus->ops_arr),
      nexus_hook(app_tid) {
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
  transport = new TTr(app_tid, phy_port);

  huge_alloc = new HugeAlloc(kInitialHugeAllocSize, numa_node,
                             transport->reg_mr_func, transport->dereg_mr_func);

  try {
    /* Complete transport initialization using the hugepage allocator */
    transport->init_hugepage_structures(huge_alloc, rx_ring);
  } catch (std::runtime_error e) {
    /* Free any huge pages that \p transport might have created */
    delete huge_alloc;
    throw e;
  }

  /* Register the hook with the Nexus */
  nexus->register_hook(&nexus_hook);

  erpc_dprintf("eRPC Rpc: Created with app TID = %u.\n", app_tid);
}

template <class TTr>
Rpc<TTr>::~Rpc() {
  /* XXX: Check if all sessions are disconnected */
  for (Session *session : session_vec) {
    if (session != nullptr) {
      _unused(session);
    }
  }

  erpc_dprintf("eRPC Rpc: Destroying for app TID = %u.\n", app_tid);

  /*
   * First delete the hugepage allocator. This deregisters and deletes the
   * SHM regions. Deregistration is done using \p transport's deregistration
   * function, so \p transport is deleted later.
   */
  delete huge_alloc;

  /* Allow \p transport to clean up non-hugepage structures */
  delete transport;

  nexus->unregister_hook(&nexus_hook);
}

template <class TTr>
void Rpc<TTr>::bury_session(Session *session) {
  assert(session != nullptr);

  /* First, free session resources */
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    /* Free the preallocated MsgBuffer */
    MsgBuffer &msg_buf = session->sslot_arr[i].app_resp.pre_resp_msgbuf;
    free_msg_buffer(msg_buf);

    /* XXX: Who frees the other message buffers in the slot? */
  }

  /* Second, mark the session as NULL in the session vector */
  uint16_t session_num;
  if (session->is_client()) {
    assert(!mgmt_retry_queue_contains(session));
    session_num = session->client.session_num;
  } else {
    /* Server-mode sessions can never be in the retry queue */
    session_num = session->server.session_num;
  }

  session_vec.at(session_num) = nullptr;
  delete session;
}

template <class TTr>
void Rpc<TTr>::handle_session_management() {
  assert(nexus_hook.sm_pkt_list.size > 0);
  nexus_hook.sm_pkt_list.lock();

  /* Handle all session management requests */
  for (SessionMgmtPkt *sm_pkt : nexus_hook.sm_pkt_list.list) {
    /* The sender of a packet cannot be this Rpc */
    if (session_mgmt_pkt_type_is_req(sm_pkt->pkt_type)) {
      assert(!(strcmp(sm_pkt->client.hostname, nexus->hostname.c_str()) == 0 &&
               sm_pkt->client.app_tid == app_tid));
    } else {
      assert(!(strcmp(sm_pkt->server.hostname, nexus->hostname.c_str()) == 0 &&
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

    /* Free packet memory that was allocated by the Nexus */
    free(sm_pkt);
  }

  /* Clear the session management packet list */
  nexus_hook.sm_pkt_list.size = 0;
  nexus_hook.sm_pkt_list.list.clear();
  nexus_hook.sm_pkt_list.unlock();
}

}  // End ERpc

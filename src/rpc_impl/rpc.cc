/*
 * @file rpc.cc
 * @brief Simple Rpc-related methods.
 */
#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "rpc.h"

namespace ERpc {

template <class TTr>
Rpc<TTr>::Rpc(Nexus<TTr> *nexus, void *context, uint8_t rpc_id,
              session_mgmt_handler_t session_mgmt_handler, uint8_t phy_port,
              size_t numa_node)
    : nexus(nexus),
      context(context),
      rpc_id(rpc_id),
      session_mgmt_handler(session_mgmt_handler),
      phy_port(phy_port),
      numa_node(numa_node),
      multi_threaded(nexus->num_bg_threads > 0),
      req_func_arr(nexus->req_func_arr) {
  // Ensure that we're running as root
  if (getuid()) {
    throw std::runtime_error("eRPC Rpc: You need to be root to use eRPC");
    return;
  }

  if (nexus == nullptr) {
    throw std::invalid_argument("eRPC Rpc: Invalid nexus");
    return;
  }

  if (rpc_id == kInvalidRpcId || nexus->rpc_id_exists(rpc_id)) {
    throw std::invalid_argument("eRPC Rpc: Invalid rpc_id");
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

  tls_registry = &nexus->tls_registry;
  tls_registry->init();  // Initialize thread-local variables for this thread
  creator_tiny_tid = get_tiny_tid();

  in_event_loop = false;

  // Partially initialize the transport without using hugepages. This
  // initializes the transport's memory registration functions required for
  // the hugepage allocator.
  transport = new TTr(rpc_id, phy_port);

  huge_alloc = new HugeAlloc(kInitialHugeAllocSize, numa_node,
                             transport->reg_mr_func, transport->dereg_mr_func);

  try {
    // Complete transport initialization using the hugepage allocator
    transport->init_hugepage_structures(huge_alloc, rx_ring);
  } catch (std::runtime_error e) {
    // Free any huge pages that \p transport might have created
    delete huge_alloc;
    throw e;
  }

  // Register the hook with the Nexus + sanity-check background request lists
  nexus_hook.rpc_id = rpc_id;
  nexus->register_hook(&nexus_hook);
  for (size_t i = 0; i < nexus->num_bg_threads; i++) {
    assert(nexus_hook.bg_req_list_arr[i] != nullptr);
  }

  erpc_dprintf("eRPC Rpc: Created with ID = %u, tiny TID = %zu.\n", rpc_id,
               creator_tiny_tid);
}

template <class TTr>
Rpc<TTr>::~Rpc() {
  // Rpc can only be destroyed from the creator thread
  if (unlikely(!in_creator())) {
    erpc_dprintf("eRPC Rpc %u: Error. Cannot destroy from background thread.\n",
                 rpc_id);
    exit(-1);
  }

  // Rpc cannot be destroyed from within the event loop (e.g., in a request
  // handler). However, event loop entrance tracking is enabled only in
  // kDatapathChecks mode
  if (kDatapathChecks && in_event_loop) {
    erpc_dprintf("eRPC Rpc %u: Error. Cannot destroy when inside event loop.\n",
                 rpc_id);
    exit(-1);
  }

  // XXX: Check if all sessions are disconnected
  for (Session *session : session_vec) {
    if (session != nullptr) {
      _unused(session);
    }
  }

  erpc_dprintf("eRPC Rpc: Destroying Rpc ID %u.\n", rpc_id);

  // First delete the hugepage allocator. This deregisters and deletes the
  // SHM regions. Deregistration is done using \p transport's deregistration
  // function, so \p transport is deleted later.
  delete huge_alloc;

  // Allow \p transport to clean up non-hugepage structures
  delete transport;

  nexus->unregister_hook(&nexus_hook);
}

template <class TTr>
void Rpc<TTr>::bury_session_st(Session *session) {
  assert(in_creator());
  assert(session != nullptr);

  if (session->is_client()) {
    assert(!session->client_info.sm_request_pending);
  }

  // Free session resources
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    // Free the preallocated MsgBuffer
    MsgBuffer &msg_buf = session->sslot_arr[i].pre_resp_msgbuf;
    free_msg_buffer(msg_buf);

    // XXX: Which other MsgBuffers do we need to free? Which MsgBuffers are
    // guaranteed to have been freed at this point?
  }

  // No need to lock the session to nullify it
  session_vec.at(session->local_session_num) = nullptr;
  delete session;  // This does nothing
}

template <class TTr>
void Rpc<TTr>::handle_session_management_st() {
  assert(in_creator());
  assert(nexus_hook.sm_rx_list.size > 0);
  nexus_hook.sm_rx_list.lock();

  // Handle all session management requests
  for (typename Nexus<TTr>::SmWorkItem &wi : nexus_hook.sm_rx_list.list) {
    SessionMgmtPkt *sm_pkt = wi.sm_pkt;
    assert(sm_pkt != nullptr);
    assert(session_mgmt_pkt_type_is_valid(sm_pkt->pkt_type));

    // The sender of a packet cannot be this Rpc
    if (sm_pkt->is_req()) {
      assert(!(strcmp(sm_pkt->client.hostname, nexus->hostname.c_str()) == 0 &&
               sm_pkt->client.rpc_id == rpc_id));
    } else {
      assert(!(strcmp(sm_pkt->server.hostname, nexus->hostname.c_str()) == 0 &&
               sm_pkt->server.rpc_id == rpc_id));
    }

    switch (sm_pkt->pkt_type) {
      case SessionMgmtPktType::kConnectReq:
        handle_connect_req_st(&wi);
        break;
      case SessionMgmtPktType::kConnectResp:
        handle_connect_resp_st(sm_pkt);
        break;
      case SessionMgmtPktType::kDisconnectReq:
        handle_disconnect_req_st(&wi);
        break;
      case SessionMgmtPktType::kDisconnectResp:
        handle_disconnect_resp_st(sm_pkt);
        break;
      default:
        assert(false);
        break;
    }

    // Free the packet memory allocated by the SM thread
    delete sm_pkt;
  }

  // Clear the session management RX list
  nexus_hook.sm_rx_list.locked_clear();
  nexus_hook.sm_rx_list.unlock();
}

template <class TTr>
void Rpc<TTr>::enqueue_sm_req(Session *session, SessionMgmtPktType pkt_type) {
  assert(session != nullptr && session->is_client());

  SessionMgmtPkt *sm_pkt = new SessionMgmtPkt();  // Freed by SM thread
  sm_pkt->pkt_type = pkt_type;
  sm_pkt->client = session->client;
  sm_pkt->server = session->server;
  nexus_hook.sm_tx_list->unlocked_push_back(
      typename Nexus<TTr>::SmWorkItem(rpc_id, sm_pkt, nullptr));
}

template <class TTr>
void Rpc<TTr>::enqueue_sm_resp(typename Nexus<TTr>::SmWorkItem *req_wi,
                               SessionMgmtErrType err_type) {
  assert(req_wi != nullptr);
  assert(req_wi->peer != nullptr);

  SessionMgmtPkt *req_sm_pkt = req_wi->sm_pkt;
  assert(req_sm_pkt->is_req());

  // Copy the request - this gets freed by the SM thread
  auto *resp_sm_pkt = new SessionMgmtPkt();
  *resp_sm_pkt = *req_sm_pkt;

  // Change the packet type to response
  resp_sm_pkt->pkt_type =
      session_mgmt_pkt_type_req_to_resp(req_sm_pkt->pkt_type);
  resp_sm_pkt->err_type = err_type;

  typename Nexus<TTr>::SmWorkItem wi(rpc_id, resp_sm_pkt, req_wi->peer);
  nexus_hook.sm_tx_list->unlocked_push_back(wi);
}

}  // End ERpc

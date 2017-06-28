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
              sm_handler_t sm_handler, uint8_t phy_port, size_t numa_node)
    : nexus(nexus),
      context(context),
      rpc_id(rpc_id),
      sm_handler(sm_handler),
      phy_port(phy_port),
      numa_node(numa_node),
      multi_threaded(nexus->num_bg_threads > 0),
      pkt_loss_epoch_cycles(kPktLossEpochMs * 1000000 * nexus->freq_ghz),
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
  creator_etid = get_etid();

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

  erpc_dprintf("eRPC Rpc: Created with ID = %u, ERpc TID = %zu.\n", rpc_id,
               creator_etid);

  prev_epoch_ts = rdtsc();  // Assign epoch timestamp as late as possible
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
}  // End ERpc

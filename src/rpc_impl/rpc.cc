/*
 * @file rpc.cc
 * @brief Simple Rpc-related methods.
 */
#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "rpc.h"

namespace erpc {

template <class TTr>
Rpc<TTr>::Rpc(Nexus *nexus, void *context, uint8_t rpc_id,
              sm_handler_t sm_handler, uint8_t phy_port, size_t numa_node)
    : nexus(nexus),
      context(context),
      rpc_id(rpc_id),
      sm_handler(sm_handler),
      phy_port(phy_port),
      numa_node(numa_node),
      creation_tsc(rdtsc()),
      multi_threaded(nexus->num_bg_threads > 0),
      pkt_loss_epoch_cycles(kPktLossEpochMs * 1000000 * nexus->freq_ghz),
      req_func_arr(nexus->req_func_arr) {
  rt_assert(!getuid(), "You need to be root to use eRPC");
  rt_assert(rpc_id != kInvalidRpcId, "Invalid Rpc ID");
  rt_assert(!nexus->rpc_id_exists(rpc_id), "Rpc ID already exists");
  rt_assert(phy_port < kMaxPhyPorts, "Invalid physical port");
  rt_assert(numa_node < kMaxNumaNodes, "Invalid NUMA node");

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

  // Register the hook with the Nexus. This installs SM and bg command queues.
  nexus_hook.rpc_id = rpc_id;
  nexus->register_hook(&nexus_hook);

  LOG_INFO("eRPC Rpc: Created with ID = %u, eRPC TID = %zu.\n", rpc_id,
           creator_etid);

  prev_epoch_ts = rdtsc();  // Assign epoch timestamp as late as possible
}

template <class TTr>
Rpc<TTr>::~Rpc() {
  // Rpc can only be destroyed from the creator thread
  if (unlikely(!in_creator())) {
    LOG_ERROR("eRPC Rpc %u: Error. Cannot destroy from background thread.\n",
              rpc_id);
    exit(-1);
  }

  // Rpc cannot be destroyed from within the event loop (e.g., in a request
  // handler). However, event loop entrance tracking is enabled only in
  // kDatapathChecks mode
  if (kDatapathChecks && in_event_loop) {
    LOG_ERROR("eRPC Rpc %u: Error. Cannot destroy when inside event loop.\n",
              rpc_id);
    exit(-1);
  }

  // XXX: Check if all sessions are disconnected
  for (Session *session : session_vec) {
    if (session != nullptr) {
      _unused(session);
    }
  }

  LOG_INFO("eRPC Rpc: Destroying Rpc ID %u.\n", rpc_id);

  // First delete the hugepage allocator. This deregisters and deletes the
  // SHM regions. Deregistration is done using \p transport's deregistration
  // function, so \p transport is deleted later.
  delete huge_alloc;

  // Allow \p transport to clean up non-hugepage structures
  delete transport;

  nexus->unregister_hook(&nexus_hook);
}

template <class TTr>
void Rpc<TTr>::nano_sleep(size_t ns) {
  size_t start = rdtsc();
  size_t end = start;
  size_t upp = static_cast<size_t>(nexus->freq_ghz * ns);
  while (end - start < upp) {
    end = rdtsc();
  }
}

template <class TTr>
double Rpc<TTr>::sec_since_creation() {
  return to_sec(rdtsc() - creation_tsc, nexus->freq_ghz);
}

template <class TTr>
double Rpc<TTr>::usec_since_creation() {
  return to_usec(rdtsc() - creation_tsc, nexus->freq_ghz);
}

}  // End erpc

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
              sm_handler_t sm_handler, uint8_t phy_port)
    : nexus(nexus),
      context(context),
      rpc_id(rpc_id),
      sm_handler(sm_handler),
      phy_port(phy_port),
      numa_node(nexus->numa_node),
      creation_tsc(rdtsc()),
      multi_threaded(nexus->num_bg_threads > 0),
      freq_ghz(nexus->freq_ghz),
      rpc_rto_cycles(us_to_cycles(kRpcRTOUs, freq_ghz)),
      rpc_pkt_loss_scan_cycles(rpc_rto_cycles / 10),
      req_func_arr(nexus->req_func_arr) {
  rt_assert(!getuid(), "You need to be root to use eRPC");
  rt_assert(rpc_id != kInvalidRpcId, "Invalid Rpc ID");
  rt_assert(!nexus->rpc_id_exists(rpc_id), "Rpc ID already exists");
  rt_assert(phy_port < kMaxPhyPorts, "Invalid physical port");
  rt_assert(numa_node < kMaxNumaNodes || numa_node == kNoNumaNode, "Invalid NUMA node");

  tls_registry = &nexus->tls_registry;
  tls_registry->init();  // Initialize thread-local variables for this thread
  creator_etid = get_etid();

  if (LOG_LEVEL >= LOG_LEVEL_REORDER) {
    auto trace_filename = "/tmp/erpc_trace_" +
                          std::to_string(nexus->sm_udp_port) +
                          std::to_string(rpc_id);
    trace_file = fopen(trace_filename.c_str(), "w");
    if (trace_file == nullptr) {
      delete huge_alloc;
      throw std::runtime_error("Failed to open trace file");
    }
  }

  // Partially initialize the transport without using hugepages. This
  // initializes the transport's memory registration functions required for
  // the hugepage allocator.
  transport =
      new TTr(nexus->sm_udp_port, rpc_id, phy_port, numa_node, trace_file);

  huge_alloc = new HugeAlloc(kInitialHugeAllocSize, numa_node,
                             transport->reg_mr_func, transport->dereg_mr_func);

  // Complete transport initialization using the hugepage allocator
  transport->init_hugepage_structures(huge_alloc, rx_ring);

  wheel = nullptr;
  if (kCcPacing) {
    timing_wheel_args_t args;
    args.freq_ghz = freq_ghz;
    args.huge_alloc = huge_alloc;

    wheel = new TimingWheel(args);
  }

  // Create DMA-registered msgbufs for control packets
  for (MsgBuffer &ctrl_msgbuf : ctrl_msgbufs) {
    ctrl_msgbuf = alloc_msg_buffer(8);  // alloc_msg_buffer() requires size > 0
    if (ctrl_msgbuf.buf == nullptr) {
      delete huge_alloc;
      throw std::runtime_error(
          std::string("Failed to allocate control msgbufs. ") +
          HugeAlloc::alloc_fail_help_str);
    }
  }

  // Register the hook with the Nexus. This installs SM and bg command queues.
  nexus_hook.rpc_id = rpc_id;
  nexus->register_hook(&nexus_hook);

  LOG_INFO("Rpc %u created. eRPC TID = %zu.\n", rpc_id, creator_etid);

  active_rpcs_root_sentinel.client_info.next = &active_rpcs_tail_sentinel;
  active_rpcs_root_sentinel.client_info.prev = nullptr;
  active_rpcs_tail_sentinel.client_info.next = nullptr;
  active_rpcs_tail_sentinel.client_info.prev = &active_rpcs_root_sentinel;

  // Steps that should be done as late as possible
  pkt_loss_scan_tsc = rdtsc();  // Assign epoch timestamp as late as possible
  if (kCcPacing) wheel->catchup();  // Wheel could be lagging, so catch up
}

template <class TTr>
Rpc<TTr>::~Rpc() {
  assert(in_dispatch());

  // XXX: Check if all sessions are disconnected
  for (Session *session : session_vec) {
    if (session != nullptr) delete session;
  }

  LOG_INFO("Destroying Rpc %u.\n", rpc_id);

  // First delete the hugepage allocator. This deregisters and deletes the
  // SHM regions. Deregistration is done using \p transport's deregistration
  // function, so \p transport is deleted later.
  delete huge_alloc;

  // Allow \p transport to clean up non-hugepage structures
  delete transport;

  nexus->unregister_hook(&nexus_hook);

  if (LOG_LEVEL >= LOG_LEVEL_REORDER) fclose(trace_file);
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

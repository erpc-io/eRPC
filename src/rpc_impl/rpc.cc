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
    : nexus_(nexus),
      context_(context),
      rpc_id_(rpc_id),
      sm_handler_(sm_handler),
      phy_port_(phy_port),
      numa_node_(nexus->numa_node_),
      creation_tsc_(rdtsc()),
      multi_threaded_(nexus->num_bg_threads_ > 0),
      freq_ghz_(nexus->freq_ghz_),
      rpc_rto_cycles_(us_to_cycles(kRpcRTOUs, freq_ghz_)),
      rpc_pkt_loss_scan_cycles_(rpc_rto_cycles_ / 10),
      req_func_arr_(nexus->req_func_arr_) {
#ifndef _WIN32
  rt_assert(!getuid(), "You need to be root to use eRPC");
#endif
  rt_assert(rpc_id != kInvalidRpcId, "Invalid Rpc ID");
  rt_assert(!nexus->rpc_id_exists(rpc_id), "Rpc ID already exists");
  rt_assert(phy_port < kMaxPhyPorts, "Invalid physical port");
  rt_assert(numa_node_ < kMaxNumaNodes, "Invalid NUMA node");

  tls_registry_ = &nexus->tls_registry_;
  tls_registry_->init();  // Initialize thread-local variables for this thread
  creator_etid_ = get_etid();

  if (ERPC_LOG_LEVEL >= ERPC_LOG_LEVEL_REORDER) {
    const auto trace_filename = "/tmp/erpc_trace_" +
                                std::to_string(nexus->sm_udp_port_) + "-rpc_" +
                                std::to_string(rpc_id);
    trace_file_ = fopen(trace_filename.c_str(), "w");
    if (trace_file_ == nullptr) {
      delete huge_alloc_;
      throw std::runtime_error("Failed to open trace file");
    }
  }

  // Partially initialize the transport without using hugepages. This
  // initializes the transport's memory registration functions required for
  // the hugepage allocator.
  transport_ =
      new TTr(nexus->sm_udp_port_, rpc_id, phy_port, numa_node_, trace_file_);

  huge_alloc_ =
      new HugeAlloc(kInitialHugeAllocSize, numa_node_, transport_->reg_mr_func_,
                    transport_->dereg_mr_func_);

  // Complete transport initialization using the hugepage allocator
  transport_->init_hugepage_structures(huge_alloc_, rx_ring_);

  wheel_ = nullptr;
  if (kCcPacing) {
    timing_wheel_args_t args;
    args.freq_ghz_ = freq_ghz_;
    args.huge_alloc_ = huge_alloc_;

    wheel_ = new TimingWheel(args);
  }

  // Create DMA-registered msgbufs for control packets
  for (MsgBuffer &ctrl_msgbuf : ctrl_msgbufs_) {
    ctrl_msgbuf = alloc_msg_buffer(8);  // alloc_msg_buffer() requires size > 0
    if (ctrl_msgbuf.buf_ == nullptr) {
      delete huge_alloc_;
      throw std::runtime_error(
          std::string("Failed to allocate control msgbufs. ") +
          HugeAlloc::kAllocFailHelpStr);
    }
  }

  // Register the hook with the Nexus. This installs SM and bg command queues.
  nexus_hook_.rpc_id_ = rpc_id;
  nexus->register_hook(&nexus_hook_);

  ERPC_INFO("Rpc %u created. eRPC TID = %zu.\n", rpc_id, creator_etid_);

  active_rpcs_root_sentinel_.client_info_.next_ = &active_rpcs_tail_sentinel_;
  active_rpcs_root_sentinel_.client_info_.prev_ = nullptr;
  active_rpcs_tail_sentinel_.client_info_.next_ = nullptr;
  active_rpcs_tail_sentinel_.client_info_.prev_ = &active_rpcs_root_sentinel_;

  // Steps that should be done as late as possible
  pkt_loss_scan_tsc_ = rdtsc();  // Assign epoch timestamp as late as possible
  if (kCcPacing) wheel_->catchup();  // Wheel could be lagging, so catch up
}

template <class TTr>
Rpc<TTr>::~Rpc() {
  assert(in_dispatch());

  // XXX: Check if all sessions are disconnected
  for (Session *session : session_vec_) {
    if (session != nullptr) delete session;
  }

  ERPC_INFO("Destroying Rpc %u.\n", rpc_id_);

  // First delete the hugepage allocator. This deregisters and deletes the
  // SHM regions. Deregistration is done using \p transport's deregistration
  // function, so \p transport is deleted later.
  delete huge_alloc_;

  // Allow \p transport to clean up non-hugepage structures
  delete transport_;

  nexus_->unregister_hook(&nexus_hook_);

  if (ERPC_LOG_LEVEL >= ERPC_LOG_LEVEL_REORDER) fclose(trace_file_);
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

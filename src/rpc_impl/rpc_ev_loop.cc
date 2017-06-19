#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::run_event_loop_one_st() {
  dpath_stat_inc(dpath_stats.ev_loop_calls, 1);

  // In kDatapathChecks mode, alert user if background thread calls event loop
  if (kDatapathChecks) {
    if (unlikely(!in_creator())) {
      throw std::runtime_error("eRPC Rpc: Event loop invoked from background.");
    }
  } else {
    assert(in_creator());
  }

  // In kDatapathChecks mode, track event loop reentrance
  if (kDatapathChecks) {
    if (unlikely(in_event_loop)) {
      throw std::runtime_error("eRPC Rpc: Re-entering event loop not allowed.");
    }
    in_event_loop = true;
  }

  // Handle session management events, if any
  if (unlikely(nexus_hook.sm_rx_list.size > 0)) {
    handle_sm_st();  // Callee grabs the hook lock
  }

  // Check for packet loss if we're in a new epoch
  size_t cur_ts = rdtsc();
  if (cur_ts - prev_epoch_ts >= pkt_loss_epoch_cycles) {
    if (!(kFaultInjection && faults.disable_pkt_loss_handling)) {
      pkt_loss_scan_reqs_st();
    }
    prev_epoch_ts = cur_ts;
  }

  process_comps_st();
  process_req_txq_st();

  if (small_rpc_unlikely(multi_threaded)) {
    // Process the background queues
    process_bg_queues_enqueue_request_st();
    process_bg_queues_enqueue_response_st();
    process_bg_queues_release_response_st();
  }

  // Drain all packets
  if (tx_batch_i > 0) do_tx_burst_st();

  if (kDatapathChecks) {
    in_event_loop = false;
  }
}

template <class TTr>
void Rpc<TTr>::run_event_loop_st() {
  assert(in_creator());
  while (true) {
    run_event_loop_one();
  }
}

template <class TTr>
void Rpc<TTr>::run_event_loop_timeout_st(size_t timeout_ms) {
  assert(in_creator());

  uint64_t start_tsc = rdtsc();
  size_t iters = 0;
  while (true) {
    run_event_loop_one();  // Run at least once even if timeout_ms is 0
    iters++;

    // Amortize timer overhead over event loop iterations
    if (iters == 1000) {
      double elapsed_ms = to_sec(rdtsc() - start_tsc, nexus->freq_ghz) * 1000;
      if (elapsed_ms > timeout_ms) return;
      iters = 0;
    }
  }
}

}  // End ERpc

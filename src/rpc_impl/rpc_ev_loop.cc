#include "rpc.h"

namespace ERpc {

template <class TTr>
void Rpc<TTr>::run_event_loop_one_st() {
  assert(in_creator());

  dpath_stat_inc(dpath_stats.ev_loop_calls, 1);

  // Handle session management events, if any
  if (unlikely(nexus_hook.sm_rx_list.size > 0)) {
    handle_sm_st();  // Callee grabs the hook lock
  }

  if (ev_loop_ticker == kEvLoopTickerReset) {
    // Check for packet loss if we're in a new epoch
    size_t cur_ts = rdtsc();

    if (cur_ts - prev_epoch_ts >= pkt_loss_epoch_cycles) {
      // Scan for packet loss unless a disablng fault has been injected by user
      if (!(kFaultInjection && faults.disable_pkt_loss_handling)) {
        pkt_loss_scan_reqs_st();
      }
      prev_epoch_ts = cur_ts;
    }
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
}

template <class TTr>
void Rpc<TTr>::run_event_loop_timeout_st(size_t timeout_ms) {
  assert(in_creator());

  // This is the only point-of-entrance to the event loop for eRPC users.
  // So, in_event_loop should be modified only here.
  if (kDatapathChecks) {
    assert(!in_event_loop);
    in_event_loop = true;
  }

  uint64_t start_tsc = rdtsc();
  while (true) {
    ev_loop_ticker++;
    run_event_loop_one_st();  // Run at least once even if timeout_ms is 0

    // Amortize timer overhead over event loop iterations
    if (ev_loop_ticker == kEvLoopTickerReset) {
      ev_loop_ticker = 0;
      double elapsed_ms = to_sec(rdtsc() - start_tsc, nexus->freq_ghz) * 1000;
      if (elapsed_ms > timeout_ms) break;
    }
  }

  if (kDatapathChecks) in_event_loop = false;
}

}  // End ERpc

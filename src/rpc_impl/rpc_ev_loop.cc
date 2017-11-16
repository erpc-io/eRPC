#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::run_event_loop_do_one_st() {
  assert(in_dispatch());

  dpath_stat_inc(dpath_stats.ev_loop_calls, 1);

  // Handle any new session management packets
  if (unlikely(nexus_hook.sm_rx_queue.size > 0)) handle_sm_rx_st();

  if (ev_loop_ticker >= kEvLoopTickerReset) {
    // Check for packet loss if we're in a new epoch
    size_t cur_ts = rdtsc();

    if (cur_ts - prev_epoch_ts >= pkt_loss_epoch_cycles) {
      pkt_loss_scan_reqs_st();
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
void Rpc<TTr>::run_event_loop_once_st() {
  assert(in_dispatch());
  ev_loop_ticker++;            // Needed for packet loss handling
  run_event_loop_do_one_st();  // Run at least once even if timeout_ms is 0
}

template <class TTr>
void Rpc<TTr>::run_event_loop_timeout_st(size_t timeout_ms) {
  assert(in_dispatch());

  uint64_t start_tsc = rdtsc();
  while (true) {
    ev_loop_ticker++;
    run_event_loop_do_one_st();  // Run at least once even if timeout_ms is 0

    // Amortize timer overhead over event loop iterations
    if (ev_loop_ticker >= kEvLoopTickerReset) {
      ev_loop_ticker = 0;
      double elapsed_ms = to_sec(rdtsc() - start_tsc, nexus->freq_ghz) * 1000;
      if (elapsed_ms > timeout_ms) break;
    }
  }
}

}  // End erpc

#include "rpc.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::run_event_loop_do_one_st() {
  assert(in_dispatch());
  dpath_stat_inc(dpath_stats_.ev_loop_calls_, 1);

  // Handle any new session management packets
  if (unlikely(nexus_hook_.sm_rx_queue_.size_ > 0)) handle_sm_rx_st();

  // The packet RX code uses ev_loop_tsc as the RX timestamp, so it must be
  // next to ev_loop_tsc stamping.
  ev_loop_tsc_ = dpath_rdtsc();
  process_comps_st();  // RX

  process_credit_stall_queue_st();    // TX
  if (kCcPacing) process_wheel_st();  // TX

  // Drain all packets
  if (tx_batch_i_ > 0) do_tx_burst_st();

  if (unlikely(multi_threaded_)) {
    // Process the background queues
    process_bg_queues_enqueue_request_st();
    process_bg_queues_enqueue_response_st();
  }

  // Check for packet loss if we're in a new epoch. ev_loop_tsc is stale by
  // less than one event loop iteration, which is negligible compared to epoch.
  if (unlikely(ev_loop_tsc_ - pkt_loss_scan_tsc_ > rpc_pkt_loss_scan_cycles_)) {
    pkt_loss_scan_tsc_ = ev_loop_tsc_;
    pkt_loss_scan_st();
  }
}

template <class TTr>
void Rpc<TTr>::run_event_loop_timeout_st(size_t timeout_ms) {
  assert(in_dispatch());

  size_t timeout_tsc = ms_to_cycles(timeout_ms, freq_ghz_);
  size_t start_tsc = rdtsc();  // For counting timeout_ms

  while (true) {
    run_event_loop_do_one_st();  // Run at least once even if timeout_ms is 0
    if (unlikely(ev_loop_tsc_ - start_tsc > timeout_tsc)) break;
  }
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

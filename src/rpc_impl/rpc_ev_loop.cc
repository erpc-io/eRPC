#include "rpc.h"

namespace ERpc {

// The Rpc object stays valid for this iteration of the event loop, i.e., it
// cannot be destroyed
template <class TTr>
inline void Rpc<TTr>::run_event_loop_one_st() {
  dpath_stat_inc(&dpath_stats.ev_loop_calls);

  // In kDatapathChecks mode, alert user if background thread calls event loop
  if (kDatapathChecks) {
    if (unlikely(!in_creator())) {
      erpc_dprintf(
          "eRPC Rpc %u: Error. Event loop invoked from a background thread.\n",
          rpc_id);
      exit(-1);
    }
  } else {
    assert(in_creator());
  }

  // In kDatapathChecks mode, track event loop reentrance
  if (kDatapathChecks) {
    if (unlikely(in_event_loop)) {
      erpc_dprintf("eRPC Rpc %u: Error. Re-entering event loop not allowed.\n",
                   rpc_id);
      exit(-1);
    }

    in_event_loop = true;
  }

  // Handle session management events, if any
  if (unlikely(nexus_hook.sm_rx_list.size > 0)) {
    handle_sm_st();  // Callee grabs the hook lock
  }

  process_comps_st();    // All RX
  process_req_txq_st();  // Req TX

  if (small_rpc_unlikely(multi_threaded)) {
    process_bg_resp_txq_st();  // Background resp TX
  }

  // Drain all packets
  if (tx_batch_i > 0) {
    transport->tx_burst(tx_burst_arr, tx_batch_i);
    tx_batch_i = 0;
  }

  if (kDatapathChecks) {
    in_event_loop = false;
  }
}

template <class TTr>
inline void Rpc<TTr>::run_event_loop_st() {
  assert(in_creator());
  while (true) {
    run_event_loop_one();
  }
}

template <class TTr>
inline void Rpc<TTr>::run_event_loop_timeout_st(size_t timeout_ms) {
  assert(in_creator());

  uint64_t start_tsc = rdtsc();
  while (true) {
    run_event_loop_one();  // Run at least once even if timeout_ms is 0

    double elapsed_ms = to_sec(rdtsc() - start_tsc, nexus->freq_ghz) * 1000;
    if (elapsed_ms > timeout_ms) {
      return;
    }
  }
}

}  // End ERpc

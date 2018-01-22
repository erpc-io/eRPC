#ifndef LARGE_RPC_TPUT_H
#define LARGE_RPC_TPUT_H

#include <gflags/gflags.h>
#include <signal.h>
#include "../apps_common.h"
#include "rpc.h"
#include "util/autorun_helpers.h"
#include "util/numautils.h"

static constexpr size_t kAppReqType = 1;
static constexpr uint8_t kAppDataByte = 3;  // Data transferred in req & resp
static constexpr size_t kAppMaxConcurrency = 32;  // Outstanding reqs per thread

// Globals
volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

// Flags
DEFINE_uint64(num_threads, 0, "Number of foreground threads per process");
DEFINE_uint64(req_size, 0, "Request data size");
DEFINE_uint64(resp_size, 0, "Response data size");
DEFINE_uint64(concurrency, 0, "Concurrent batches per thread");
DEFINE_double(drop_prob, 0, "Packet drop probability");
DEFINE_string(profile, "", "Experiment profile to use");
DEFINE_double(throttle, 0, "Throttle flows to incast receiver?");
DEFINE_double(throttle_fraction, 1, "Fraction of fair share to throttle to.");

// Per-thread app context
class AppContext : public BasicAppContext {
 public:
  // We need a wide range of latency measurements: ~4 us for 4KB RPCs, to
  // >10 ms for 8MB RPCs under congestion. So erpc::Latency doesn't work here.
  std::vector<double> latency_vec;

  struct timespec tput_t0;  // Start time for throughput measurement

  size_t stat_rx_bytes_tot = 0;  // Total bytes received
  size_t stat_tx_bytes_tot = 0;  // Total bytes transmitted

  uint64_t req_ts[kAppMaxConcurrency];  // Per-request timestamps
  erpc::MsgBuffer req_msgbuf[kAppMaxConcurrency];
  erpc::MsgBuffer resp_msgbuf[kAppMaxConcurrency];
};

// Allocate request and response MsgBuffers
void alloc_req_resp_msg_buffers(AppContext *c) {
  for (size_t i = 0; i < FLAGS_concurrency; i++) {
    c->resp_msgbuf[i] = c->rpc->alloc_msg_buffer(FLAGS_resp_size);
    erpc::rt_assert(c->resp_msgbuf[i].buf != nullptr, "Alloc failed");

    c->req_msgbuf[i] = c->rpc->alloc_msg_buffer(FLAGS_req_size);
    erpc::rt_assert(c->req_msgbuf[i].buf != nullptr, "Alloc failed");

    // Fill the request regardless of kAppMemset. This is a one-time thing.
    memset(c->req_msgbuf[i].buf, kAppDataByte, FLAGS_req_size);
  }
}

#endif

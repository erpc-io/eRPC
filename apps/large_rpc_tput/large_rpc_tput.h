#ifndef LARGE_RPC_TPUT_H
#define LARGE_RPC_TPUT_H

#include <gflags/gflags.h>
#include <signal.h>
#include "../apps_common.h"
#include "rpc.h"
#include "util/autorun_helpers.h"
#include "util/misc.h"

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
DEFINE_double(session_gbps, erpc::kTimelyMaxRate, "Non-CC session throughput");

static bool validate_drop_prob(const char *, double p) {
  if (!erpc::kTesting) return p == 0.0;
  return p >= 0 && p < 1;
}

// Request tag, which doubles up as the request descriptor for the request queue
union tag_t {
  struct {
    uint64_t session_idx : 32;  // Index into context's session_num array
    uint64_t msgbuf_idx : 32;   // Index into context's req_msgbuf array
  } s;

  size_t _tag;

  tag_t(uint64_t session_idx, uint64_t msgbuf_idx) {
    s.session_idx = session_idx;
    s.msgbuf_idx = msgbuf_idx;
  }
  tag_t(size_t _tag) : _tag(_tag) {}
  tag_t() : _tag(0) {}
};
static_assert(sizeof(tag_t) == sizeof(size_t), "");

// Per-thread application context
// session_num_vec contains a hole at self_session_idx. We need for simplicity
// in the "victim" profile.
class AppContext : public BasicAppContext {
 public:
  // We need a wide range of latency measurements: ~4 us for 4KB RPCs, to
  // >10 ms for 8MB RPCs under congestion. So erpc::Latency doesn't work here.
  std::vector<double> latency_vec;

  // Index in session_num_vec that represents a self connection, if it exists
  size_t self_session_idx = SIZE_MAX;

  struct timespec tput_t0;  // Start time for throughput measurement

  size_t stat_rx_bytes_tot = 0;  // Total bytes received
  size_t stat_tx_bytes_tot = 0;  // Total bytes transmitted

  std::vector<size_t> stat_req_vec;  // Number of requests sent on a session
  std::vector<tag_t> req_vec;        // Request queue

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

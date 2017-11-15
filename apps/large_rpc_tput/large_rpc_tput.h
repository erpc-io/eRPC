#ifndef LARGE_RPC_TPUT_H
#define LARGE_RPC_TPUT_H

#include <gflags/gflags.h>
#include <signal.h>
#include "../apps_common.h"
#include "rpc.h"
#include "util/misc.h"

static constexpr size_t kAppNexusUdpPort = 31851;
static constexpr size_t kAppPhyPort = 0;
static constexpr size_t kAppNumaNode = 0;
static constexpr size_t kAppReqType = 1;
static constexpr uint8_t kAppDataByte = 3;     // Data transferred in req & resp
static constexpr size_t kMaxConcurrency = 32;  // Outstanding reqs per thread

// Globals
volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

// Flags
DEFINE_uint64(num_threads, 0, "Number of foreground threads per machine");
DEFINE_uint64(num_bg_threads, 0, "Number of background threads per machine");
DEFINE_uint64(req_size, 0, "Request data size");
DEFINE_uint64(resp_size, 0, "Response data size");
DEFINE_uint64(concurrency, 0, "Concurrent batches per thread");
DEFINE_double(drop_prob, 0, "Packet drop probability");
DEFINE_string(profile, "", "Experiment profile to use");

static bool validate_concurrency(const char *, uint64_t concurrency) {
  return concurrency <= kMaxConcurrency;
}
DEFINE_validator(concurrency, &validate_concurrency);

static bool validate_drop_prob(const char *, double p) {
  if (!erpc::kTesting) return p == 0.0;
  return p >= 0 && p < 1;
}
DEFINE_validator(drop_prob, &validate_drop_prob);

static bool validate_profile(const char *, const std::string &profile) {
  return profile == "random" || profile == "timely_small" ||
         profile == "victim";
}
DEFINE_validator(profile, &validate_profile);

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
class AppContext {
 public:
  TmpStat *tmp_stat = nullptr;
  erpc::Rpc<erpc::IBTransport> *rpc = nullptr;

  // We need a wide range of latency measurements: ~4 us for 4KB RPCs, to
  // >10 ms for 8MB RPCs under congestion. So erpc::Latency is insufficient
  // here
  std::vector<double> latency_vec;

  std::vector<int> session_num_vec;

  // Index in session_num_vec that represents a self connection, *if it exists*
  size_t self_session_idx = std::numeric_limits<size_t>::max();

  size_t thread_id;         // The ID of the thread that owns this context
  size_t num_sm_resps = 0;  // Number of SM responses
  struct timespec tput_t0;  // Start time for throughput measurement
  erpc::FastRand fastrand;

  size_t stat_rx_bytes_tot = 0;  // Total bytes received
  size_t stat_tx_bytes_tot = 0;  // Total bytes transmitted

  std::vector<size_t> stat_req_vec;  // Number of requests sent on a session
  std::vector<tag_t> req_vec;        // Request queue

  uint64_t req_ts[kMaxConcurrency];  // Per-request timestamps
  erpc::MsgBuffer req_msgbuf[kMaxConcurrency];
  erpc::MsgBuffer resp_msgbuf[kMaxConcurrency];

  ~AppContext() {
    if (tmp_stat != nullptr) delete tmp_stat;
  }
};

// Allocate request and response MsgBuffers
void alloc_req_resp_msg_buffers(AppContext *c) {
  assert(c != nullptr);
  assert(c->rpc != nullptr);

  for (size_t msgbuf_idx = 0; msgbuf_idx < FLAGS_concurrency; msgbuf_idx++) {
    c->resp_msgbuf[msgbuf_idx] = c->rpc->alloc_msg_buffer(FLAGS_resp_size);
    erpc::rt_assert(c->resp_msgbuf[msgbuf_idx].buf != nullptr, "Alloc failed");

    c->req_msgbuf[msgbuf_idx] = c->rpc->alloc_msg_buffer(FLAGS_req_size);
    erpc::rt_assert(c->req_msgbuf[msgbuf_idx].buf != nullptr, "Alloc failed");

    // Fill the request regardless of kAppMemset. This is a one-time thing.
    memset(c->req_msgbuf[msgbuf_idx].buf, kAppDataByte, FLAGS_req_size);
  }
}

#endif

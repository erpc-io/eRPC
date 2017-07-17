#ifndef LARGE_RPC_TPUT_H
#define LARGE_RPC_TPUT_H

#include <gflags/gflags.h>
#include <papi.h>
#include <signal.h>
#include "../apps_common.h"
#include "rpc.h"
#include "util/latency.h"
#include "util/misc.h"

static constexpr size_t kAppNexusUdpPort = 31851;
static constexpr size_t kAppPhyPort = 0;
static constexpr size_t kAppNumaNode = 0;
static constexpr size_t kAppReqType = 1;
static constexpr uint8_t kAppDataByte = 3;     // Data transferred in req & resp
static constexpr size_t kMaxConcurrency = 32;  // Outstanding reqs per thread

// Flags
DEFINE_uint64(num_threads, 0, "Number of foreground threads per machine");
DEFINE_uint64(num_bg_threads, 0, "Number of background threads per machine");
DEFINE_uint64(req_size, 0, "Request data size");
DEFINE_uint64(resp_size, 0, "Response data size");
DEFINE_uint64(concurrency, 0, "Concurrent batches per thread");
DEFINE_string(profile, "", "Experiment profile to use");

static bool validate_concurrency(const char *, uint64_t concurrency) {
  return concurrency <= kMaxConcurrency;
}
DEFINE_validator(concurrency, &validate_concurrency);

static bool validate_profile(const char *, const std::string &profile) {
  return profile == "random" || profile == "timely_small";
}
DEFINE_validator(profile, &validate_profile);

// Request tag, which doubles up as the request descriptor for the request queue
union tag_t {
  struct {
    uint64_t session_index : 32;  // Index into context's session_num array
    uint64_t msgbuf_index : 32;   // Index into context's req_msgbuf array
  };

  size_t _tag;

  tag_t(uint64_t session_index, uint64_t msgbuf_index)
      : session_index(session_index), msgbuf_index(msgbuf_index) {}
  tag_t(size_t _tag) : _tag(_tag) {}
  tag_t() : _tag(0) {}
};
static_assert(sizeof(tag_t) == sizeof(size_t), "");

// Per-thread application context
class AppContext {
 public:
  ERpc::Rpc<ERpc::IBTransport> *rpc = nullptr;
  ERpc::TmpStat *tmp_stat = nullptr;
  ERpc::Latency latency;

  std::vector<int> session_num_vec;

  // The entry in session_arr for this thread, so we don't send reqs to ourself
  size_t self_session_index;
  size_t thread_id;         // The ID of the thread that owns this context
  size_t num_sm_resps = 0;  // Number of SM responses
  struct timespec tput_t0;  // Start time for throughput measurement
  ERpc::FastRand fastrand;

  size_t stat_resp_rx_bytes_tot = 0;       // Total response bytes received
  size_t stat_resp_tx_bytes_tot = 0;       // Total response bytes transmitted
  std::vector<size_t> stat_resp_rx_bytes;  // Resp bytes received on a session

  std::vector<tag_t> req_vec;  // Request queue

  uint64_t req_ts[kMaxConcurrency];  // Per-request timestamps
  ERpc::MsgBuffer req_msgbuf[kMaxConcurrency];
  ERpc::MsgBuffer resp_msgbuf[kMaxConcurrency];

  ~AppContext() {
    if (tmp_stat != nullptr) delete tmp_stat;
  }
};

// The session index selection function for the "random" profile
size_t get_session_index_func_random(AppContext *c) {
  assert(c != nullptr);
  size_t num_sessions = c->session_num_vec.size();

  // Use Lemire's trick to compute random numbers modulo c->num_sessions
  uint32_t x = c->fastrand.next_u32();
  size_t rand_session_index = (static_cast<size_t>(x) * num_sessions) >> 32;
  while (rand_session_index == c->self_session_index) {
    x = c->fastrand.next_u32();
    rand_session_index = (static_cast<size_t>(x) * num_sessions) >> 32;
  }

  return rand_session_index;
}

#endif

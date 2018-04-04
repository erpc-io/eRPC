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
DEFINE_uint64(num_proc_0_threads, 0, "Threads in process 0");
DEFINE_uint64(num_proc_other_threads, 0, "Threads in process with ID != 0");
DEFINE_uint64(req_size, 0, "Request data size");
DEFINE_uint64(resp_size, 0, "Response data size");
DEFINE_uint64(concurrency, 0, "Concurrent batches per thread");
DEFINE_double(drop_prob, 0, "Packet drop probability");
DEFINE_string(profile, "", "Experiment profile to use");
DEFINE_double(throttle, 0, "Throttle flows to incast receiver?");
DEFINE_double(throttle_fraction, 1, "Fraction of fair share to throttle to.");

struct app_stats_t {
  double rx_gbps;
  double tx_gbps;
  size_t retransmissions;
  double avg_us;
  double _99_us;
  size_t pad[3];

  app_stats_t() { memset(this, 0, sizeof(app_stats_t)); }

  /// Return a space-separated string of all stats
  std::string to_string() {
    return std::to_string(rx_gbps) + " " + std::to_string(tx_gbps) + " " +
           std::to_string(retransmissions) + " " + std::to_string(avg_us) +
           " " + std::to_string(_99_us);
  }

  /// Accumulate stats
  app_stats_t& operator+=(const app_stats_t& rhs) {
    this->rx_gbps += rhs.rx_gbps;
    this->tx_gbps += rhs.tx_gbps;
    this->retransmissions += rhs.retransmissions;
    this->avg_us += rhs.avg_us;
    this->_99_us += rhs._99_us;
    return *this;
  }
};
static_assert(sizeof(app_stats_t) == 64, "");

// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  // We need a wide range of latency measurements: ~4 us for 4KB RPCs, to
  // >10 ms for 8MB RPCs under congestion. So erpc::Latency doesn't work here.
  std::vector<double> latency_vec;

  struct timespec tput_t0;  // Start time for throughput measurement
  app_stats_t* app_stats;   // Common stats array for all threads

  size_t stat_rx_bytes_tot = 0;  // Total bytes received
  size_t stat_tx_bytes_tot = 0;  // Total bytes transmitted

  uint64_t req_ts[kAppMaxConcurrency];  // Per-request timestamps
  erpc::MsgBuffer req_msgbuf[kAppMaxConcurrency];
  erpc::MsgBuffer resp_msgbuf[kAppMaxConcurrency];
};

// Allocate request and response MsgBuffers
void alloc_req_resp_msg_buffers(AppContext* c) {
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

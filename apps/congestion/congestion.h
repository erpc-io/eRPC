#ifndef CONGESTION_H
#define CONGESTION_H

#include <gflags/gflags.h>
#include <signal.h>
#include "../apps_common.h"
#include "rpc.h"
#include "util/autorun_helpers.h"
#include "util/numautils.h"

static constexpr size_t kAppEvLoopMs = 1000;  // Duration of event loop
static constexpr bool kAppVerbose = false;
static constexpr size_t kAppReqTypeIncast = 1;
static constexpr size_t kAppReqTypeRegular = 2;
static constexpr uint8_t kAppDataByte = 3;  // Data transferred in req & resp
static constexpr size_t kAppMaxConcurrency = 32;  // Max outstanding reqs/thread

// Globals
volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

// Incast flags
DEFINE_uint64(incast_threads_zero, 0, "Threads receiving incast at process 0");
DEFINE_uint64(incast_threads_other, 0, "Threads sending incast traffic");
DEFINE_uint64(incast_req_size, 0, "Incast request data size");
DEFINE_uint64(incast_resp_size, 0, "Incast response data size");
DEFINE_double(incast_throttle, 0, "If not 0, fair share fraction for incasts");

// Non-incast traffic flags
DEFINE_uint64(regular_threads_other, 0, "Threads sending regular traffic");
DEFINE_uint64(regular_concurrency, 0, "Concurrent requests per regular thread");
DEFINE_uint64(regular_req_size, 0, "Reqular request data size");
DEFINE_uint64(regular_resp_size, 0, "Regular response data size");
DEFINE_double(regular_latency_divisor, 1.0, "Latency precision factor");

size_t tot_threads_other() {
  return FLAGS_incast_threads_other + FLAGS_regular_threads_other;
}

struct app_stats_t {
  double incast_gbps;         // All incast threads
  double incast_gbps_stddev;  // Only thread 0
  size_t re_tx;               // All incast and regular threads
  double regular_50_us;       // All regular threads
  double regular_99_us;       // All regular threads
  double regular_999_us;      // All regular threads
  size_t pad[2];

  app_stats_t() { memset(this, 0, sizeof(app_stats_t)); }

  static std::string get_template_str() {
    return "incast_gbps incast_gbps_stddev re_tx regular_50_us regular_99_us "
           "regular_999_us";
  }

  /// Return a space-separated string of all stats
  std::string to_string() {
    return std::to_string(incast_gbps) + " " +
           std::to_string(incast_gbps_stddev) + " " + std::to_string(re_tx) +
           " " + std::to_string(regular_50_us) + " " +
           std::to_string(regular_99_us) + " " + std::to_string(regular_999_us);
  }

  /// Threads publish stats selectively, so must accumulate manually
  app_stats_t& operator+=(const app_stats_t& rhs) = delete;
};
static_assert(sizeof(app_stats_t) == 64, "");

// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  struct timespec tput_t0;  // Start time for measurement
  app_stats_t* app_stats;   // Common stats array for all threads

  size_t incast_tx_bytes = 0;     // Total incast bytes sent
  erpc::Latency regular_latency;  // Latency for regular traffic

  uint64_t req_ts[kAppMaxConcurrency];  // Per-request timestamps
  erpc::MsgBuffer req_msgbuf[kAppMaxConcurrency];
  erpc::MsgBuffer resp_msgbuf[kAppMaxConcurrency];
};

#endif

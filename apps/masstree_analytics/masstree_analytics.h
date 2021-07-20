#ifndef MASSTREE_ANALYTICS_H
#define MASSTREE_ANALYTICS_H

#include <gflags/gflags.h>
#include <signal.h>
#include "../apps_common.h"
#include "mt_index_api.h"
#include "rpc.h"
#include "util/latency.h"
#include "util/numautils.h"
#include "util/timer.h"

static constexpr size_t kAppPointReqType = 1;
static constexpr size_t kAppRangeReqType = 2;
static constexpr size_t kAppEvLoopMs = 500;

// Workload params
static constexpr bool kBypassMasstree = false;  // Bypass Masstree?
static constexpr size_t kAppMaxReqWindow = 16;  // Max pending reqs per client

// Globals
volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

// Flags
DEFINE_uint64(num_server_fg_threads, 0, "Number of server foreground threads");
DEFINE_uint64(num_server_bg_threads, 0, "Number of server background threads");
DEFINE_uint64(num_population_threads, 0,
              "Number of threads to use to populate the server's Masstree");
DEFINE_uint64(num_client_threads, 0, "Number of client threads");
DEFINE_uint64(req_window, 0, "Outstanding requests per client thread");
DEFINE_uint64(num_keys, 0, "Number of keys in the server's Masstree");
DEFINE_uint64(range_size, 0, "Size of range to scan");
DEFINE_uint64(range_req_percent, 0, "Percentage of range scans");

// Return true iff this machine is the one server
bool is_server() { return FLAGS_process_id == 0; }

struct wire_req_t {
  size_t req_type;
  union {
    struct {
      uint8_t key[MtIndex::kKeySize];
    } point_req;

    struct {
      uint8_t key[MtIndex::kKeySize];
      size_t range;  // The max number of keys after key to sum up
    } range_req;
  };

  std::string to_string() const {
    std::ostringstream ret;
    ret << "[Type " << (req_type == kAppPointReqType ? "point" : "range")
        << ", key: ";
    if (req_type == kAppPointReqType) {
      const uint64_t *key_64 =
          reinterpret_cast<const uint64_t *>(point_req.key);
      for (size_t i = 0; i < MtIndex::kKeySize / sizeof(uint64_t); i++) {
        ret << std::to_string(key_64[i]) << " ";
      }
    } else {
      const uint64_t *key_64 =
          reinterpret_cast<const uint64_t *>(point_req.key);
      for (size_t i = 0; i < MtIndex::kKeySize; i++) {
        ret << std::to_string(key_64[i]) << " ";
      }
    }
    ret << "]";
    return ret.str();
  }
};

enum class RespType : size_t { kFound, kNotFound };
struct wire_resp_t {
  RespType resp_type;
  union {
    uint8_t value[MtIndex::kValueSize];
    size_t range_count;  // The range sum for range queries
  };

  std::string to_string() const {
    std::ostringstream ret;
    ret << "[Value: ";
    for (size_t i = 0; i < MtIndex::kValueSize; i++) {
      ret << std::to_string(value[i]) << " ";
    }
    ret << "]";
    return ret.str();
  }
};

struct app_stats_t {
  double mrps;       // Point request rate
  double lat_us_50;  // Point request median latency
  double lat_us_90;  // Point request 90th percentile latency
  double lat_us_99;  // Point request 99th percentile latency
  size_t pad[4];

  app_stats_t() { memset(this, 0, sizeof(app_stats_t)); }

  static std::string get_template_str() {
    return "mrps lat_us_50 lat_us_90 lat_us_99";
  }

  std::string to_string() {
    return std::to_string(mrps) + " " + std::to_string(lat_us_50) + " " +
           std::to_string(lat_us_90) + " " + std::to_string(lat_us_99);
  }

  /// Accumulate stats
  app_stats_t &operator+=(const app_stats_t &rhs) {
    this->mrps += rhs.mrps;
    this->lat_us_50 += rhs.lat_us_50;
    this->lat_us_90 += rhs.lat_us_90;
    this->lat_us_99 += rhs.lat_us_99;
    return *this;
  }
};
static_assert(sizeof(app_stats_t) == 64, "");

// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  struct {
    MtIndex *mt_index = nullptr;      // The shared Masstree index
    threadinfo_t **ti_arr = nullptr;  // Thread info array, indexed by eRPC TID
  } server;

  struct {
    erpc::ChronoTimer tput_timer;  // Throughput measurement start
    app_stats_t *app_stats;        // Common stats array for all threads

    erpc::Latency point_latency;  // Latency of point requests (factor = 10)
    erpc::Latency range_latency;  // Latency of point requests (factor = 1)

    struct {
      uint32_t req_seed_;
      uint64_t req_ts_;
      erpc::MsgBuffer req_msgbuf_;
      erpc::MsgBuffer resp_msgbuf_;
    } window_[kAppMaxReqWindow];

    erpc::FastRand fast_rand;
    size_t num_resps_tot = 0;  // Total responses received (range & point reqs)
  } client;
};

// Allocate request and response MsgBuffers
void alloc_req_resp_msg_buffers(AppContext *c) {
  for (size_t msgbuf_idx = 0; msgbuf_idx < FLAGS_req_window; msgbuf_idx++) {
    c->client.window_[msgbuf_idx].req_msgbuf_ =
        c->rpc_->alloc_msg_buffer_or_die(sizeof(wire_req_t));

    c->client.window_[msgbuf_idx].resp_msgbuf_ =
        c->rpc_->alloc_msg_buffer_or_die(sizeof(wire_resp_t));
  }
}

#endif  // MASSTREE_ANALYTICS_H

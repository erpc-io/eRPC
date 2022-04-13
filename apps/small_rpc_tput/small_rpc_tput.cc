#include <gflags/gflags.h>
#include <signal.h>
#include <cstring>
#include "../apps_common.h"
#include "rpc.h"
#include "util/autorun_helpers.h"
#include "util/latency.h"
#include "util/numautils.h"

static constexpr size_t kAppEvLoopMs = 1000;  // Duration of event loop
static constexpr bool kAppVerbose = false;    // Print debug info on datapath
static constexpr bool kAppMeasureLatency = false;
static constexpr double kAppLatFac = 3.0;        // Precision factor for latency
static constexpr bool kAppPayloadCheck = false;  // Check full request/response

// Optimization knobs. Set to true to disable optimization.
static constexpr bool kAppOptDisablePreallocResp = false;
static constexpr bool kAppOptDisableRxRingReq = false;

static constexpr size_t kAppReqType = 1;    // eRPC request type
static constexpr uint8_t kAppDataByte = 3;  // Data transferred in req & resp
static constexpr size_t kAppMaxBatchSize = 32;
static constexpr size_t kAppMaxConcurrency = 128;

DEFINE_uint64(batch_size, 0, "Request batch size");
DEFINE_uint64(msg_size, 0, "Request and response size");
DEFINE_uint64(num_threads, 0, "Number of foreground threads per machine");
DEFINE_uint64(concurrency, 0, "Concurrent batches per thread");

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

union tag_t {
  struct {
    uint64_t batch_i : 32;
    uint64_t msgbuf_i : 32;
  } s;

  void *_tag;

  tag_t(uint64_t batch_i, uint64_t msgbuf_i) {
    s.batch_i = batch_i;
    s.msgbuf_i = msgbuf_i;
  }
  tag_t(void *_tag) : _tag(_tag) {}
};

static_assert(sizeof(tag_t) == sizeof(void *), "");

// Per-batch context
class BatchContext {
 public:
  size_t num_resps_rcvd = 0;         // Number of responses received
  size_t req_tsc[kAppMaxBatchSize];  // Timestamp when request was issued
  erpc::MsgBuffer req_msgbuf[kAppMaxBatchSize];
  erpc::MsgBuffer resp_msgbuf[kAppMaxBatchSize];
};

struct app_stats_t {
  double mrps;
  size_t num_re_tx;

  // Used only if latency stats are enabled
  double lat_us_50;
  double lat_us_99;
  double lat_us_999;
  double lat_us_9999;
  size_t pad[2];

  app_stats_t() { memset(this, 0, sizeof(app_stats_t)); }

  static std::string get_template_str() {
    std::string ret = "mrps num_re_tx";
    if (kAppMeasureLatency) {
      ret += " lat_us_50 lat_us_99 lat_us_999 lat_us_9999";
    }
    return ret;
  }

  std::string to_string() {
    auto ret = std::to_string(mrps) + " " + std::to_string(num_re_tx);
    if (kAppMeasureLatency) {
      return ret + " " + std::to_string(lat_us_50) + " " +
             std::to_string(lat_us_99) + " " + std::to_string(lat_us_999) +
             " " + std::to_string(lat_us_9999);
    }

    return ret;
  }

  /// Accumulate stats
  app_stats_t &operator+=(const app_stats_t &rhs) {
    this->mrps += rhs.mrps;
    this->num_re_tx += rhs.num_re_tx;
    if (kAppMeasureLatency) {
      this->lat_us_50 += rhs.lat_us_50;
      this->lat_us_99 += rhs.lat_us_99;
      this->lat_us_999 += rhs.lat_us_999;
      this->lat_us_9999 += rhs.lat_us_9999;
    }
    return *this;
  }
};
static_assert(sizeof(app_stats_t) == 64, "");

// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  struct timespec tput_t0;  // Start time for throughput measurement
  app_stats_t *app_stats;   // Common stats array for all threads

  size_t stat_resp_rx[kAppMaxConcurrency] = {0};  // Resps received for batch i
  size_t stat_resp_rx_tot = 0;  // Total responses received (all batches)
  size_t stat_req_rx_tot = 0;   // Total requests received (all batches)

  std::array<BatchContext, kAppMaxConcurrency> batch_arr;  // Per-batch context
  erpc::Latency latency;  // Cold if latency measurement disabled

  ~AppContext() {}
};

void app_cont_func(void *, void *);  // Forward declaration

// Send all requests for a batch
void send_reqs(AppContext *c, size_t batch_i) {
  assert(batch_i < FLAGS_concurrency);
  BatchContext &bc = c->batch_arr[batch_i];

  for (size_t i = 0; i < FLAGS_batch_size; i++) {
    if (kAppVerbose) {
      printf("Process %zu, Rpc %u: Sending request for batch %zu.\n",
             FLAGS_process_id, c->rpc_->get_rpc_id(), batch_i);
    }

    if (!kAppPayloadCheck) {
      bc.req_msgbuf[i].buf_[0] = kAppDataByte;  // Touch req MsgBuffer
    } else {
      // Fill the request MsgBuffer with a checkable sequence
      uint8_t *buf = bc.req_msgbuf[i].buf_;
      buf[0] = c->fastrand_.next_u32();
      for (size_t j = 1; j < FLAGS_msg_size; j++) buf[j] = buf[0] + j;
    }

    if (kAppMeasureLatency) bc.req_tsc[i] = erpc::rdtsc();

    tag_t tag(batch_i, i);
    c->rpc_->enqueue_request(c->fast_get_rand_session_num(), kAppReqType,
                             &bc.req_msgbuf[i], &bc.resp_msgbuf[i],
                             app_cont_func, reinterpret_cast<void *>(tag._tag));
  }
}

void req_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<AppContext *>(_context);
  c->stat_req_rx_tot++;

  const erpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  assert(req_msgbuf->get_data_size() == FLAGS_msg_size);

  // RX ring request optimization knob
  if (kAppOptDisableRxRingReq) {
    // Simulate copying the request off the RX ring
    auto copy_msgbuf = c->rpc_->alloc_msg_buffer(FLAGS_msg_size);
    assert(copy_msgbuf.buf != nullptr);
    memcpy(copy_msgbuf.buf_, req_msgbuf->buf_, FLAGS_msg_size);
    c->rpc_->free_msg_buffer(copy_msgbuf);
  }

  // Preallocated response optimization knob
  if (kAppOptDisablePreallocResp) {
    erpc::MsgBuffer &resp_msgbuf = req_handle->dyn_resp_msgbuf_;
    resp_msgbuf = c->rpc_->alloc_msg_buffer(FLAGS_msg_size);
    assert(resp_msgbuf.buf != nullptr);

    if (!kAppPayloadCheck) {
      resp_msgbuf.buf_[0] = req_msgbuf->buf_[0];
    } else {
      memcpy(resp_msgbuf.buf_, req_msgbuf->buf_, FLAGS_msg_size);
    }
    c->rpc_->enqueue_response(req_handle, &req_handle->dyn_resp_msgbuf_);
  } else {
    erpc::Rpc<erpc::CTransport>::resize_msg_buffer(
        &req_handle->pre_resp_msgbuf_, FLAGS_msg_size);

    if (!kAppPayloadCheck) {
      req_handle->pre_resp_msgbuf_.buf_[0] = req_msgbuf->buf_[0];
    } else {
      memcpy(req_handle->pre_resp_msgbuf_.buf_, req_msgbuf->buf_,
             FLAGS_msg_size);
    }
    c->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
  }
}

void app_cont_func(void *_context, void *_tag) {
  auto *c = static_cast<AppContext *>(_context);
  auto tag = static_cast<tag_t>(_tag);

  BatchContext &bc = c->batch_arr[tag.s.batch_i];
  const erpc::MsgBuffer &resp_msgbuf = bc.resp_msgbuf[tag.s.msgbuf_i];
  assert(resp_msgbuf.get_data_size() == FLAGS_msg_size);

  if (!kAppPayloadCheck) {
    // Do a cheap check, but touch the response MsgBuffer
    if (unlikely(resp_msgbuf.buf_[0] != kAppDataByte)) {
      fprintf(stderr, "Invalid response.\n");
      exit(-1);
    }
  } else {
    // Check the full response MsgBuffer
    for (size_t i = 0; i < FLAGS_msg_size; i++) {
      const uint8_t *buf = resp_msgbuf.buf_;
      if (unlikely(buf[i] != static_cast<uint8_t>(buf[0] + i))) {
        fprintf(stderr, "Invalid resp at %zu (%u, %u)\n", i, buf[0], buf[i]);
        exit(-1);
      }
    }
  }

  if (kAppVerbose) {
    printf("Received response for batch %u, msgbuf %u.\n", tag.s.batch_i,
           tag.s.msgbuf_i);
  }

  bc.num_resps_rcvd++;

  if (kAppMeasureLatency) {
    size_t req_tsc = bc.req_tsc[tag.s.msgbuf_i];
    double req_lat_us =
        erpc::to_usec(erpc::rdtsc() - req_tsc, c->rpc_->get_freq_ghz());
    c->latency.update(static_cast<size_t>(req_lat_us * kAppLatFac));
  }

  if (bc.num_resps_rcvd == FLAGS_batch_size) {
    // If we have a full batch, reset batch progress and send more requests
    bc.num_resps_rcvd = 0;
    send_reqs(c, tag.s.batch_i);
  }

  c->stat_resp_rx_tot++;
  c->stat_resp_rx[tag.s.batch_i]++;
}

void connect_sessions(AppContext &c) {
  // Create a session to each thread in the cluster except to:
  // (a) all threads on this machine if DPDK is used (because no loopback), or
  // (b) this thread if a non-DPDK transport is used.
  for (size_t p_i = 0; p_i < FLAGS_num_processes; p_i++) {
    if ((erpc::CTransport::kTransportType == erpc::TransportType::kDPDK) &&
        (p_i == FLAGS_process_id)) {
      continue;
    }

    std::string remote_uri = erpc::get_uri_for_process(p_i);

    if (FLAGS_sm_verbose == 1) {
      printf("Process %zu, thread %zu: Creating sessions to %s.\n",
             FLAGS_process_id, c.thread_id, remote_uri.c_str());
    }

    for (size_t t_i = 0; t_i < FLAGS_num_threads; t_i++) {
      if (FLAGS_process_id == p_i && c.thread_id_ == t_i) continue;
      int session_num = c.rpc_->create_session(remote_uri, t_i);
      erpc::rt_assert(session_num >= 0, "Failed to create session");
      c.session_num_vec_.push_back(session_num);
    }
  }

  while (c.num_sm_resps_ != c.session_num_vec_.size()) {
    c.rpc_->run_event_loop(kAppEvLoopMs);
    if (unlikely(ctrl_c_pressed == 1)) return;
  }
}

void print_stats(AppContext &c) {
  double seconds = erpc::sec_since(c.tput_t0);

  // Min/max responses for a concurrent batch, to check for stagnated batches
  size_t max_resps = 0, min_resps = SIZE_MAX;
  for (size_t i = 0; i < FLAGS_concurrency; i++) {
    min_resps = std::min(min_resps, c.stat_resp_rx[i]);
    max_resps = std::max(max_resps, c.stat_resp_rx[i]);
    c.stat_resp_rx[i] = 0;
  }

  // Session throughput percentiles, used if rate computation is enabled
  std::vector<double> session_tput;
  if (erpc::kCcRateComp) {
    for (int session_num : c.session_num_vec) {
      erpc::Timely *timely = c.rpc->get_timely(session_num);
      session_tput.push_back(timely->get_rate_gbps());
    }
    std::sort(session_tput.begin(), session_tput.end());
  }

  double tput_mrps = c.stat_resp_rx_tot / (seconds * 1000000);
  c.app_stats[c.thread_id].mrps = tput_mrps;
  c.app_stats[c.thread_id].num_re_tx = c.rpc->pkt_loss_stats.num_re_tx;
  if (kAppMeasureLatency) {
    c.app_stats[c.thread_id].lat_us_50 = c.latency.perc(0.50) / kAppLatFac;
    c.app_stats[c.thread_id].lat_us_99 = c.latency.perc(0.99) / kAppLatFac;
    c.app_stats[c.thread_id].lat_us_999 = c.latency.perc(0.999) / kAppLatFac;
    c.app_stats[c.thread_id].lat_us_9999 = c.latency.perc(0.9999) / kAppLatFac;
  }

  size_t num_sessions = c.session_num_vec.size();

  // Optional stats
  char lat_stat[100];
  sprintf(lat_stat, "[%.2f, %.2f us]", c.latency.perc(.50) / kAppLatFac,
          c.latency.perc(.99) / kAppLatFac);
  char rate_stat[100];
  sprintf(rate_stat, "[%.2f, %.2f, %.2f, %.2f Gbps]",
          erpc::kCcRateComp ? session_tput.at(num_sessions * 0.00) : -1,
          erpc::kCcRateComp ? session_tput.at(num_sessions * 0.05) : -1,
          erpc::kCcRateComp ? session_tput.at(num_sessions * 0.50) : -1,
          erpc::kCcRateComp ? session_tput.at(num_sessions * 0.95) : -1);

  printf(
      "Process %zu, thread %zu: %.3f Mrps, re_tx = %zu, still_in_wheel = %zu. "
      "RX: %zuK resps, %zuK reqs. Resps/batch: min %zuK, max %zuK. "
      "Latency: %s. Rate = %s.\n",
      FLAGS_process_id, c.thread_id, tput_mrps,
      c.app_stats[c.thread_id].num_re_tx,
      c.rpc->pkt_loss_stats.still_in_wheel_during_retx,
      c.stat_resp_rx_tot / 1000, c.stat_req_rx_tot / 1000, min_resps / 1000,
      max_resps / 1000, kAppMeasureLatency ? lat_stat : "N/A",
      erpc::kCcRateComp ? rate_stat : "N/A");

  if (c.thread_id == 0) {
    app_stats_t accum;
    for (size_t i = 0; i < FLAGS_num_threads; i++) accum += c.app_stats[i];
    if (kAppMeasureLatency) {
      accum.lat_us_50 /= FLAGS_num_threads;
      accum.lat_us_99 /= FLAGS_num_threads;
      accum.lat_us_999 /= FLAGS_num_threads;
      accum.lat_us_9999 /= FLAGS_num_threads;
    }
    c.tmp_stat->write(accum.to_string());
  }

  c.stat_resp_rx_tot = 0;
  c.stat_req_rx_tot = 0;
  c.rpc->pkt_loss_stats.num_re_tx = 0;
  c.latency.reset();

  clock_gettime(CLOCK_REALTIME, &c.tput_t0);
}

// The function executed by each thread in the cluster
void thread_func(size_t thread_id, app_stats_t *app_stats, erpc::Nexus *nexus) {
  AppContext c;
  c.thread_id = thread_id;
  c.app_stats = app_stats;
  if (thread_id == 0) c.tmp_stat = new TmpStat(app_stats_t::get_template_str());

  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  erpc::rt_assert(port_vec.size() > 0);
  uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c),
                                  static_cast<uint8_t>(thread_id),
                                  basic_sm_handler, phy_port);

  rpc.retry_connect_on_invalid_rpc_id = true;
  c.rpc = &rpc;

  // Pre-allocate request and response MsgBuffers for each batch
  for (size_t i = 0; i < FLAGS_concurrency; i++) {
    BatchContext &bc = c.batch_arr[i];
    for (size_t j = 0; j < FLAGS_batch_size; j++) {
      bc.req_msgbuf[j] = rpc.alloc_msg_buffer_or_die(FLAGS_msg_size);
      bc.resp_msgbuf[j] = rpc.alloc_msg_buffer_or_die(FLAGS_msg_size);
    }
  }

  connect_sessions(c);

  printf("Process %zu, thread %zu: All sessions connected. Starting work.\n",
         FLAGS_process_id, thread_id);

  // Start work
  clock_gettime(CLOCK_REALTIME, &c.tput_t0);
  for (size_t i = 0; i < FLAGS_concurrency; i++) send_reqs(&c, i);

  for (size_t i = 0; i < FLAGS_test_ms; i += 1000) {
    rpc.run_event_loop(kAppEvLoopMs);  // 1 second
    if (ctrl_c_pressed == 1) break;
    print_stats(c);
  }
}

int main(int argc, char **argv) {
  signal(SIGINT, ctrl_c_handler);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  erpc::rt_assert(FLAGS_batch_size <= kAppMaxBatchSize, "Invalid batch size");
  erpc::rt_assert(FLAGS_concurrency <= kAppMaxConcurrency, "Invalid cncrrncy.");
  erpc::rt_assert(FLAGS_numa_node <= 1, "Invalid NUMA node");

  // We create a bit fewer sessions
  const size_t num_sessions = 2 * FLAGS_num_processes * FLAGS_num_threads;
  erpc::rt_assert(num_sessions * erpc::kSessionCredits <=
                      erpc::Transport::kNumRxRingEntries,
                  "Too few ring buffers");

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_numa_node, 0);
  nexus.register_req_func(kAppReqType, req_handler);
  nexus.register_req_func(kPingReqHandlerType, ping_req_handler);

  std::vector<std::thread> threads(FLAGS_num_threads);
  auto *app_stats = new app_stats_t[FLAGS_num_threads];

  for (size_t i = 0; i < FLAGS_num_threads; i++) {
    threads[i] = std::thread(thread_func, i, app_stats, &nexus);
    erpc::bind_to_core(threads[i], FLAGS_numa_node, i);
  }

  for (auto &thread : threads) thread.join();
  delete[] app_stats;
}

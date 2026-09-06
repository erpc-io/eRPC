#include "util/hdr_export.h"
#include "util/latency.h"

#include <gflags/gflags.h>
#include <signal.h>

#include <cstring>

#include "../apps_common.h"
#include "HdrHistogram_c/src/hdr_histogram.h"
#include "rpc.h"
#include "util/autorun_helpers.h"
#include "util/numautils.h"

static constexpr size_t kAppEvLoopMs = 1000;  // Duration of event loop
static constexpr bool kAppVerbose = false;    // Print debug info on datapath
static constexpr size_t kAppReqType = 1;      // eRPC request type
static constexpr size_t kAppStartReqSize = 64;
static constexpr size_t kAppEndReqSize = 1024;

// Precision factor for latency measurement
static constexpr double kAppLatFac = erpc::kIsAzure ? 1.0 : 10.0;

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

DEFINE_uint64(num_server_processes, 1, "Number of server processes");
DEFINE_uint64(resp_size, 8, "Size of the server's RPC response in bytes");

DEFINE_string(histogram_file, "", "Optional end-of-run HDR CSV path (fixed/preflight client only)");

DEFINE_uint64(req_size, 0,
              "Fixed request bytes (64 to 1024); 0 preserves size cycling");
DEFINE_uint64(warmup_ms, 2000,
              "Warmup before fixed-size measurement, in multiples of 1000 ms");

class ServerContext : public BasicAppContext {
 public:
  erpc::FastRand fast_rand_;
};

class ClientContext : public BasicAppContext {
  static constexpr int64_t kMinLatencyMicros = 1;
  static constexpr int64_t kMaxLatencyMicros = 1000 * 1000 * 100;  // 100 sec
  static constexpr int64_t kLatencyPrecision = 2;  // Two significant digits

 public:
  size_t start_tsc_;
  size_t req_size_;  // Between kAppStartReqSize and kAppEndReqSize
  erpc::MsgBuffer req_msgbuf_, resp_msgbuf_;
  hdr_histogram *latency_hist_;
  bool discard_next_sample_ = false;
  size_t latency_samples_ = 0;
  size_t latency_samples_prev_ = 0;

  // If true, the client doubles its request size (up to kAppEndReqSize) when
  // issuing the next request, and resets this flag to false
  bool double_req_size_ = false;

  ClientContext() {
    int ret = hdr_init(kMinLatencyMicros, kMaxLatencyMicros, kLatencyPrecision,
                       &latency_hist_);
    erpc::rt_assert(ret == 0, "Failed to initialize latency histogram");
  }

  ~ClientContext() { hdr_close(latency_hist_); }
};

void req_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<ServerContext *>(_context);
  erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf_,
                                                 FLAGS_resp_size);
  c->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

// Rpc construction may change the calling thread's affinity during DPDK EAL
// initialization. Pin from the worker afterwards, before any benchmark traffic.
static void pin_benchmark_worker() {
  const auto cores = erpc::get_lcores_for_numa_node(FLAGS_numa_node);
  erpc::rt_assert(!cores.empty(), "No CPUs on requested NUMA node");
  const size_t index = FLAGS_process_id % cores.size();
  erpc::bind_current_thread_to_core(FLAGS_numa_node, index);
  printf("Latency: Worker pinned after transport initialization to CPU %zu\n",
         cores.at(index));
}

void server_func(erpc::Nexus *nexus) {
  printf("Latency: Running server, process ID %zu\n", FLAGS_process_id);
  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  uint8_t phy_port = port_vec.at(0);

  ServerContext c;
  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c), 0 /* tid */,
                                  basic_sm_handler, phy_port);
  pin_benchmark_worker();
  rpc.set_pre_resp_msgbuf_size(FLAGS_resp_size);
  c.rpc_ = &rpc;

  while (true) {
    rpc.run_event_loop(1000);
    if (ctrl_c_pressed == 1) return;
  }
}

void connect_sessions(ClientContext &c) {
  for (size_t i = 0; i < FLAGS_num_server_processes; i++) {
    const std::string server_uri = erpc::get_uri_for_process(i);
    printf("Process %zu: Creating session to %s.\n", FLAGS_process_id,
           server_uri.c_str());

    const int session_num =
        c.rpc_->create_session(server_uri, 0 /* tid at server */);
    erpc::rt_assert(session_num >= 0, "Failed to create session");
    c.session_num_vec_.push_back(session_num);

    while (c.num_sm_resps_ != (i + 1)) {
      c.rpc_->run_event_loop(kAppEvLoopMs);
      if (unlikely(ctrl_c_pressed == 1)) return;
    }
  }
}

void app_cont_func(void *, void *);
inline void send_req(ClientContext &c) {
  if (c.double_req_size_) {
    c.double_req_size_ = false;
    c.req_size_ *= 2;
    if (c.req_size_ > kAppEndReqSize) c.req_size_ = kAppStartReqSize;

    c.rpc_->resize_msg_buffer(&c.req_msgbuf_, c.req_size_);
    c.rpc_->resize_msg_buffer(&c.resp_msgbuf_, FLAGS_resp_size);
  }

  c.start_tsc_ = erpc::rdtsc();
  const size_t server_id = c.fastrand_.next_u32() % FLAGS_num_server_processes;
  c.rpc_->enqueue_request(c.session_num_vec_[server_id], kAppReqType,
                          &c.req_msgbuf_, &c.resp_msgbuf_, app_cont_func,
                          nullptr);
  if (kAppVerbose) {
    printf("Latency: Sending request of size %zu bytes to server #%zu\n",
           c.req_msgbuf_.get_data_size(), server_id);
  }
}

void app_cont_func(void *_context, void *) {
  auto *c = static_cast<ClientContext *>(_context);
  assert(c->resp_msgbuf_.get_data_size() == FLAGS_resp_size);

  if (kAppVerbose) {
    printf("Latency: Received response of size %zu bytes\n",
           c->resp_msgbuf_.get_data_size());
  }

  const double req_lat_us =
      erpc::to_usec(erpc::rdtsc() - c->start_tsc_, c->rpc_->get_freq_ghz());

  if (c->discard_next_sample_) {
    c->discard_next_sample_ = false;
  } else {
    erpc::rt_assert(hdr_record_value(c->latency_hist_,
                     static_cast<int64_t>(req_lat_us * kAppLatFac)),
                     "Latency exceeds histogram range");
    c->latency_samples_++;
  }

  send_req(*c);
}

void client_func(erpc::Nexus *nexus) {
  printf("Latency: Running client, process ID %zu\n", FLAGS_process_id);
  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  uint8_t phy_port = port_vec.at(0);

  ClientContext c;
  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c), 0,
                                  basic_sm_handler, phy_port);

  pin_benchmark_worker();
  rpc.retry_connect_on_invalid_rpc_id_ = true;
  c.rpc_ = &rpc;
  c.req_size_ = FLAGS_req_size == 0 ? kAppStartReqSize : FLAGS_req_size;

  c.req_msgbuf_ = rpc.alloc_msg_buffer_or_die(kAppEndReqSize);
  c.resp_msgbuf_ = rpc.alloc_msg_buffer_or_die(FLAGS_resp_size);

  rpc.resize_msg_buffer(&c.req_msgbuf_, c.req_size_);
  rpc.resize_msg_buffer(&c.resp_msgbuf_, FLAGS_resp_size);

  connect_sessions(c);
  if (ctrl_c_pressed == 1) return;

  printf("Latency: Process %zu: Session connected. Starting work.\n",
         FLAGS_process_id);
  printf(
      "req_size median_us 5th_us 99th_us 99.9th_us 99.99th_us 99.999th_us "
      "99.9999th_us max_us [new samples, total_samples in distribution, "
      "total_time]\n");

  send_req(c);
  if (FLAGS_req_size != 0) {
    printf("Latency: Warming up for %zu ms at %zu request bytes\n",
           FLAGS_warmup_ms, c.req_size_);
    for (size_t elapsed = 0; elapsed < FLAGS_warmup_ms; elapsed += 1000) {
      rpc.run_event_loop(kAppEvLoopMs);
      if (ctrl_c_pressed == 1) return;
    }
    hdr_reset(c.latency_hist_);
    c.latency_samples_ = 0;
    c.latency_samples_prev_ = 0;
    // Exclude the one outstanding RPC that straddles the warmup boundary.
    c.discard_next_sample_ = FLAGS_warmup_ms != 0;
    printf("Latency: Measuring for %zu ms; cumulative fixed-size histogram\n",
           FLAGS_test_ms);
  }
  for (size_t i = 0; i < FLAGS_test_ms; i += 1000) {
    rpc.run_event_loop(kAppEvLoopMs);  // 1 second
    if (ctrl_c_pressed == 1) return;

    if (c.latency_samples_ == c.latency_samples_prev_) {
      erpc::rt_assert(FLAGS_histogram_file.empty(), "No responses during histogram measurement");
      printf("No new responses in %.2f seconds\n", kAppEvLoopMs / 1000.0);
      fprintf(stderr, "No new responses in %.2f seconds\n",
              kAppEvLoopMs / 1000.0);
      fflush(stderr);
    } else {
      printf(
          "%zu %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f "
          "[%zu new samples, %zu total samples, %zu seconds]\n",
          c.req_size_,
          hdr_value_at_percentile(c.latency_hist_, 50.0) / kAppLatFac,
          hdr_value_at_percentile(c.latency_hist_, 5.0) / kAppLatFac,
          hdr_value_at_percentile(c.latency_hist_, 99) / kAppLatFac,
          hdr_value_at_percentile(c.latency_hist_, 99.9) / kAppLatFac,
          hdr_value_at_percentile(c.latency_hist_, 99.99) / kAppLatFac,
          hdr_value_at_percentile(c.latency_hist_, 99.999) / kAppLatFac,
          hdr_value_at_percentile(c.latency_hist_, 99.9999) / kAppLatFac,
          hdr_max(c.latency_hist_) / kAppLatFac,
          c.latency_samples_ - c.latency_samples_prev_, c.latency_samples_,
          (i + 1000) / 1000);
    }
    fflush(stdout);

    // Warmup for the first two seconds. Also, reset percentiles every minute.
    const size_t seconds = i / 1000;
    if (FLAGS_req_size == 0 && (seconds < 2 || (seconds % 60 == 0))) {
      hdr_reset(c.latency_hist_);
      c.latency_samples_ = 0;
      c.latency_samples_prev_ = 0;
    }

    c.latency_samples_prev_ = c.latency_samples_;
    c.double_req_size_ = FLAGS_req_size == 0;
  }
  erpc::export_hdr(FLAGS_histogram_file, c.latency_hist_, kAppLatFac,
      "# benchmark=erpc_latency\n# request_bytes=" + std::to_string(FLAGS_req_size) +
      "\n# response_bytes=" + std::to_string(FLAGS_resp_size) + "\n# window=1\n" +
      "# warmup_ms=" + std::to_string(FLAGS_warmup_ms) +
      "\n# measurement_ms=" + std::to_string(FLAGS_test_ms) + "\n");
}

int main(int argc, char **argv) {
  signal(SIGINT, ctrl_c_handler);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  erpc::rt_assert(FLAGS_histogram_file.empty() || (FLAGS_req_size != 0 && FLAGS_process_id >= FLAGS_num_server_processes),
                  "Histogram export requires a fixed-size/preflight client");
  erpc::rt_assert(FLAGS_numa_node <= 1, "Invalid NUMA node");
  erpc::rt_assert(FLAGS_req_size == 0 ||
                      (FLAGS_req_size >= kAppStartReqSize &&
                       FLAGS_req_size <= kAppEndReqSize),
                  "--req_size must be 0 or between 64 and 1024");
  if (FLAGS_req_size != 0) {
    erpc::rt_assert(FLAGS_test_ms > 0 && FLAGS_test_ms % 1000 == 0 &&
                        FLAGS_warmup_ms % 1000 == 0,
                    "Fixed-size mode requires positive --test_ms and durations "
                    "in multiples of 1000 ms");
  }
  printf("Latency: Starting latency test. Response size = %zu bytes\n",
         FLAGS_resp_size);

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_numa_node, 0);
  nexus.register_req_func(kAppReqType, req_handler);

  auto t = std::thread(
      FLAGS_process_id < FLAGS_num_server_processes ? server_func : client_func,
      &nexus);

  t.join();
}

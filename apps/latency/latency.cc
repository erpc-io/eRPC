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
static constexpr size_t kAppMinReqSize = 64;
static constexpr size_t kAppMaxReqSize = 1024;

// Precision factor for latency measurement
static constexpr double kAppLatFac = erpc::kIsAzure ? 1.0 : 10.0;

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

DEFINE_uint64(num_server_processes, 1, "Number of server processes");
DEFINE_uint64(resp_size, 8, "Size of the server's RPC response in bytes");

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
  size_t req_size_;  // Between kAppMinReqSize and kAppMaxReqSize
  erpc::MsgBuffer req_msgbuf_, resp_msgbuf_;
  hdr_histogram *latency_hist_;
  size_t latency_samples_ = 0;
  size_t latency_samples_prev_ = 0;

  // If true, the client doubles its request size (up to kAppMaxReqSize) when
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
  // erpc::nano_sleep((c->fast_rand_.next_u32() % 5) * 1000 * 1000, 3.0);
  c->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

void server_func(erpc::Nexus *nexus) {
  printf("Latency: Running server, process ID %zu\n", FLAGS_process_id);
  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  uint8_t phy_port = port_vec.at(0);

  ServerContext c;
  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c), 0 /* tid */,
                                  basic_sm_handler, phy_port);
  rpc.set_pre_resp_msgbuf_size(FLAGS_resp_size);
  c.rpc_ = &rpc;

  while (true) {
    rpc.run_event_loop(1000);
    if (ctrl_c_pressed == 1) break;
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
    if (c.req_size_ > kAppMaxReqSize) c.req_size_ = kAppMinReqSize;

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

  hdr_record_value(c->latency_hist_,
                   static_cast<int64_t>(req_lat_us * kAppLatFac));
  c->latency_samples_++;

  send_req(*c);
}

void client_func(erpc::Nexus *nexus) {
  printf("Latency: Running client, process ID %zu\n", FLAGS_process_id);
  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  uint8_t phy_port = port_vec.at(0);

  ClientContext c;
  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c), 0,
                                  basic_sm_handler, phy_port);

  rpc.retry_connect_on_invalid_rpc_id_ = true;
  c.rpc_ = &rpc;
  c.req_size_ = kAppMinReqSize;

  c.req_msgbuf_ = rpc.alloc_msg_buffer_or_die(kAppMaxReqSize);
  c.resp_msgbuf_ = rpc.alloc_msg_buffer_or_die(FLAGS_resp_size);

  connect_sessions(c);

  printf("Latency: Process %zu: Session connected. Starting work.\n",
         FLAGS_process_id);
  printf(
      "req_size median_us 5th_us 99th_us 99.9th_us 99.99th_us 99.999th_us "
      "99.9999th_us max_us [new samples, total_samples in distribution, "
      "total_time]\n");

  send_req(c);
  for (size_t i = 0; i < FLAGS_test_ms; i += 1000) {
    rpc.run_event_loop(kAppEvLoopMs);  // 1 second
    if (ctrl_c_pressed == 1) break;

    if (c.latency_samples_ == c.latency_samples_prev_) {
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
          i / 1000);
    }
    fflush(stdout);

    // Warmup for the first two seconds. Also, reset percentiles every minute.
    const size_t seconds = i / 1000;
    if (seconds < 2 || (seconds % 60 == 0)) {
      hdr_reset(c.latency_hist_);
      c.latency_samples_ = 0;
      c.latency_samples_prev_ = 0;
    }

    c.latency_samples_prev_ = c.latency_samples_;
    c.double_req_size_ = true;
  }
}

int main(int argc, char **argv) {
  printf("Latency: Welcome!");
  signal(SIGINT, ctrl_c_handler);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  erpc::rt_assert(FLAGS_numa_node <= 1, "Invalid NUMA node");

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_numa_node, 0);
  nexus.register_req_func(kAppReqType, req_handler);

  auto t = std::thread(
      FLAGS_process_id < FLAGS_num_server_processes ? server_func : client_func,
      &nexus);

  const size_t num_socket_cores =
      erpc::get_lcores_for_numa_node(FLAGS_numa_node).size();
  const size_t affinity_core = FLAGS_process_id % num_socket_cores;
  printf("Latency: Will run on CPU core %zu\n", affinity_core);
  if (FLAGS_process_id >= num_socket_cores) {
    fprintf(stderr,
            "Latency: Warning: The number of latency processes is close to "
            "this machine's core count. This could be fine, but to ensure good "
            "performance, please double-check for core collision.\n");
  }

  erpc::bind_to_core(t, FLAGS_numa_node, affinity_core);
  t.join();
}

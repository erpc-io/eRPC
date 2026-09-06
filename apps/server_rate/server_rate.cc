#include "util/hdr_export.h"
#include <gflags/gflags.h>
#include <signal.h>
#include <cstring>
#include "../apps_common.h"
#include "rpc.h"
#include "HdrHistogram_c/src/hdr_histogram.h"
#include "util/autorun_helpers.h"
#include "util/latency.h"
#include "util/numautils.h"
#include "util/timer.h"

static constexpr size_t kAppEvLoopMs = 1000;     // Duration of event loop
static constexpr bool kAppVerbose = false;       // Print debug info on datapath
static constexpr double kAppLatFac = 10.0;       // Precision factor for latency
static constexpr size_t kAppReqType = 1;         // eRPC request type
static constexpr size_t kAppMaxWindowSize = 32;  // Max pending reqs per client

DEFINE_uint64(num_server_threads, 1, "Number of threads at the server machine");
DEFINE_uint64(num_client_threads, 1, "Number of threads per client machine");
DEFINE_uint64(window_size, 1, "Outstanding requests per client");
DEFINE_string(histogram_file, "", "Optional end-of-run HDR CSV path (fixed/preflight client only)");

DEFINE_uint64(req_size, 64, "Size of request message in bytes");
DEFINE_uint64(resp_size, 32, "Size of response message in bytes ");

DEFINE_bool(single_host_preflight, false,
            "Opt-in two-process, one-worker-per-process test; server CPU index 0, "
            "client CPU index 1; warmup and decimal Mrps/HDR summaries");
DEFINE_uint64(preflight_warmup_ms, 5000,
              "Warmup for single_host_preflight, in multiples of 1000 ms");

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

class ServerContext : public BasicAppContext {
 public:
  size_t num_resps = 0;
};

class ClientContext : public BasicAppContext {
 public:
  size_t num_resps = 0;
  size_t thread_id;
  erpc::ChronoTimer start_time[kAppMaxWindowSize];
  erpc::Latency latency;
  erpc::MsgBuffer req_msgbuf[kAppMaxWindowSize], resp_msgbuf[kAppMaxWindowSize];
  hdr_histogram *preflight_hist = nullptr;
  bool measuring = false;
  bool discard_latency[kAppMaxWindowSize] = {};
  ClientContext() {
    if (FLAGS_single_host_preflight) {
      erpc::rt_assert(hdr_init(1, 1000000000, 2, &preflight_hist) == 0,
                      "Failed to allocate preflight histogram");
    }
  }
  ~ClientContext() {
    if (preflight_hist != nullptr) hdr_close(preflight_hist);
  }
};

void req_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<ServerContext *>(_context);
  c->num_resps++;

  erpc::Rpc<erpc::CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf_,
                                                 FLAGS_resp_size);
  c->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

static size_t worker_core_index(size_t thread_id) {
  return FLAGS_single_host_preflight ? FLAGS_process_id : thread_id;
}

static void pin_worker(size_t thread_id) {
  const size_t index = worker_core_index(thread_id);
  erpc::bind_current_thread_to_core(FLAGS_numa_node, index);
  const auto cores = erpc::get_lcores_for_numa_node(FLAGS_numa_node);
  printf("ServerRate: process %zu thread %zu pinned after transport init to CPU %zu\n",
         FLAGS_process_id, thread_id, cores.at(index));
}

void server_func(erpc::Nexus *nexus, size_t thread_id) {
  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  uint8_t phy_port = port_vec.at(0);

  ServerContext c;
  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c), thread_id,
                                  basic_sm_handler, phy_port);
  pin_worker(thread_id);
  c.rpc_ = &rpc;

  while (true) {
    c.num_resps = 0;
    erpc::ChronoTimer start;
    rpc.run_event_loop(kAppEvLoopMs);

    const double seconds = start.get_sec();
    printf("thread %zu: %.2f %s. rx batch %.2f, tx batch %.2f\n", thread_id,
           c.num_resps / (seconds * (FLAGS_single_host_preflight ? 1e6 : Mi(1))),
           FLAGS_single_host_preflight ? "Mrps" : "M/s", c.rpc_->get_avg_rx_batch(),
           c.rpc_->get_avg_tx_batch());

    c.rpc_->reset_dpath_stats();
    c.num_resps = 0;

    if (ctrl_c_pressed == 1) break;
  }
}

void app_cont_func(void *, void *);
inline void send_req(ClientContext &c, size_t ws_i) {
  c.start_time[ws_i].reset();
  c.rpc_->enqueue_request(c.fast_get_rand_session_num(), kAppReqType,
                         &c.req_msgbuf[ws_i], &c.resp_msgbuf[ws_i],
                         app_cont_func, reinterpret_cast<void *>(ws_i));
}

void app_cont_func(void *_context, void *_ws_i) {
  auto *c = static_cast<ClientContext *>(_context);
  const auto ws_i = reinterpret_cast<size_t>(_ws_i);
  assert(c->resp_msgbuf[ws_i].get_data_size() == FLAGS_resp_size);

  const double req_lat_us = c->start_time[ws_i].get_us();
  if (FLAGS_single_host_preflight) {
    if (c->measuring && !c->discard_latency[ws_i]) {
      erpc::rt_assert(hdr_record_value(c->preflight_hist,
                      std::max<int64_t>(1, static_cast<int64_t>(req_lat_us * kAppLatFac))),
                      "Preflight latency exceeds histogram range");
    }
    c->discard_latency[ws_i] = false;
  } else {
    c->latency.update(static_cast<size_t>(req_lat_us * kAppLatFac));
  }
  c->num_resps++;

  send_req(*c, ws_i);  // Clock the used window slot
}

// Connect this client thread to all server threads
void create_sessions(ClientContext &c) {
  std::string server_uri = erpc::get_uri_for_process(0);
  if (FLAGS_sm_verbose == 1) {
    printf("Process %zu: Creating %zu sessions to %s.\n", FLAGS_process_id,
           FLAGS_num_server_threads, server_uri.c_str());
  }

  for (size_t i = 0; i < FLAGS_num_server_threads; i++) {
    int session_num = c.rpc_->create_session(server_uri, i);
    erpc::rt_assert(session_num >= 0, "Failed to create session");
    c.session_num_vec_.push_back(session_num);
  }

  while (c.num_sm_resps_ != FLAGS_num_server_threads) {
    c.rpc_->run_event_loop(kAppEvLoopMs);
    if (unlikely(ctrl_c_pressed == 1)) return;
  }
}

void client_func(erpc::Nexus *nexus, size_t thread_id) {
  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  uint8_t phy_port = port_vec.at(0);

  ClientContext c;
  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c), thread_id,
                                  basic_sm_handler, phy_port);

  pin_worker(thread_id);
  rpc.retry_connect_on_invalid_rpc_id_ = true;
  c.rpc_ = &rpc;
  c.thread_id = thread_id;

  create_sessions(c);
  if (ctrl_c_pressed == 1) return;

  printf("Process %zu, thread %zu: Connected. Starting work.\n",
         FLAGS_process_id, thread_id);
  if (thread_id == 0 && !FLAGS_single_host_preflight) {
    printf("thread_id: median_us 5th_us 99th_us 999th_us Mops\n");
  }

  for (size_t i = 0; i < FLAGS_window_size; i++) {
    c.req_msgbuf[i] = rpc.alloc_msg_buffer_or_die(FLAGS_req_size);
    c.resp_msgbuf[i] = rpc.alloc_msg_buffer_or_die(FLAGS_resp_size);
    send_req(c, i);
  }

  if (FLAGS_single_host_preflight) {
    printf("Preflight: warmup=%zu ms measurement=%zu ms req=%zu resp=%zu window=%zu\n",
           FLAGS_preflight_warmup_ms, FLAGS_test_ms, FLAGS_req_size,
           FLAGS_resp_size, FLAGS_window_size);
    for (size_t ms = 0; ms < FLAGS_preflight_warmup_ms; ms += 1000) {
      rpc.run_event_loop(kAppEvLoopMs);
      if (ctrl_c_pressed == 1) return;
    }
    c.num_resps = 0;
    c.measuring = true;
    for (size_t slot = 0; slot < FLAGS_window_size; ++slot) {
      c.discard_latency[slot] = FLAGS_preflight_warmup_ms != 0;
    }
    size_t total = 0;
    erpc::ChronoTimer measured;
    printf("PREFLIGHT_INTERVAL window completed interval_mrps cumulative_median_us "
           "cumulative_p99_us cumulative_p99.9_us elapsed_s\n");
    for (size_t ms = 0; ms < FLAGS_test_ms; ms += 1000) {
      erpc::ChronoTimer interval;
      rpc.run_event_loop(kAppEvLoopMs);
      const double seconds = interval.get_sec();
      if (ctrl_c_pressed == 1) return;
      erpc::rt_assert(c.num_resps > 0, "Preflight stalled: no completed RPCs in interval");
      total += c.num_resps;
      printf("PREFLIGHT_INTERVAL %zu %zu %.6f %.1f %.1f %.1f %.3f\n",
             FLAGS_window_size, c.num_resps, c.num_resps / (seconds * 1e6),
             hdr_value_at_percentile(c.preflight_hist, 50) / kAppLatFac,
             hdr_value_at_percentile(c.preflight_hist, 99) / kAppLatFac,
             hdr_value_at_percentile(c.preflight_hist, 99.9) / kAppLatFac,
             measured.get_sec());
      c.num_resps = 0;
      fflush(stdout);
    }
    const double seconds = measured.get_sec();
    erpc::rt_assert(c.preflight_hist->total_count > 0, "No post-warmup latency samples");
    printf("PREFLIGHT_SUMMARY req_bytes=%zu resp_bytes=%zu window=%zu "
           "completed=%zu elapsed_s=%.6f mrps=%.6f median_us=%.1f p99_us=%.1f "
           "p99.9_us=%.1f max_us=%.1f latency_samples=%lld\n",
           FLAGS_req_size, FLAGS_resp_size, FLAGS_window_size, total, seconds,
           total / (seconds * 1e6),
           hdr_value_at_percentile(c.preflight_hist, 50) / kAppLatFac,
           hdr_value_at_percentile(c.preflight_hist, 99) / kAppLatFac,
           hdr_value_at_percentile(c.preflight_hist, 99.9) / kAppLatFac,
           hdr_max(c.preflight_hist) / kAppLatFac,
           static_cast<long long>(c.preflight_hist->total_count));
    erpc::export_hdr(FLAGS_histogram_file, c.preflight_hist, kAppLatFac,
        "# benchmark=server_rate\n# request_bytes=" + std::to_string(FLAGS_req_size) +
        "\n# response_bytes=" + std::to_string(FLAGS_resp_size) +
        "\n# window=" + std::to_string(FLAGS_window_size) +
        "\n# warmup_ms=" + std::to_string(FLAGS_preflight_warmup_ms) +
        "\n# measurement_ms=" + std::to_string(FLAGS_test_ms) + "\n");
    return;
  }

  for (size_t i = 0; i < FLAGS_test_ms; i += 1000) {
    erpc::ChronoTimer start;
    rpc.run_event_loop(kAppEvLoopMs);  // 1 second
    if (ctrl_c_pressed == 1) break;

    const double seconds = start.get_sec();
    printf("%zu: %.1f %.1f %.1f %.1f %.2f\n", thread_id,
           c.latency.perc(.5) / kAppLatFac, c.latency.perc(.05) / kAppLatFac,
           c.latency.perc(.99) / kAppLatFac, c.latency.perc(.999) / kAppLatFac,
           c.num_resps / (seconds * Mi(1)));

    c.num_resps = 0;
    c.latency.reset();
  }
}

int main(int argc, char **argv) {
  signal(SIGINT, ctrl_c_handler);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  erpc::rt_assert(FLAGS_histogram_file.empty() || (FLAGS_single_host_preflight && FLAGS_process_id == 1),
                  "Histogram export requires a fixed-size/preflight client");

  erpc::rt_assert(FLAGS_numa_node <= 1, "Invalid NUMA node");
  erpc::rt_assert(FLAGS_resp_size <= erpc::Rpc<erpc::CTransport>::get_max_data_per_pkt(),
                  "Response must fit in the preallocated single-packet buffer");
  erpc::rt_assert(FLAGS_window_size >= 1 && FLAGS_window_size <= kAppMaxWindowSize,
                  "Window must be between 1 and 32");
  erpc::rt_assert(FLAGS_num_server_threads > 0 && FLAGS_num_client_threads > 0 &&
                      FLAGS_num_server_threads <= erpc::kMaxRpcId + 1 &&
                      FLAGS_num_client_threads <= erpc::kMaxRpcId + 1,
                  "Invalid worker thread count");
  erpc::rt_assert(FLAGS_req_size > 0 &&
                      FLAGS_req_size <= erpc::Rpc<erpc::CTransport>::kMaxMsgSize &&
                      FLAGS_resp_size > 0, "Invalid request/response size");
  const auto ports = flags_get_numa_ports(FLAGS_numa_node);
  erpc::rt_assert(!ports.empty() && ports.front() < erpc::kMaxPhyPorts,
                  "Specify a valid port with --numa_0_ports or --numa_1_ports");
  const auto cores = erpc::get_lcores_for_numa_node(FLAGS_numa_node);
  const size_t num_workers = FLAGS_process_id == 0 ? FLAGS_num_server_threads
                                                 : FLAGS_num_client_threads;
  erpc::rt_assert(num_workers <= cores.size(), "More workers than NUMA-local CPUs");
  if (FLAGS_single_host_preflight) {
    erpc::rt_assert(FLAGS_process_id <= 1 && FLAGS_num_server_threads == 1 &&
                        FLAGS_num_client_threads == 1 && cores.size() >= 2,
                    "Preflight requires process IDs 0/1, one worker each, and two CPUs");
    erpc::rt_assert(FLAGS_test_ms > 0 && FLAGS_test_ms % 1000 == 0 &&
                        FLAGS_preflight_warmup_ms % 1000 == 0,
                    "Preflight needs positive test_ms and durations in whole seconds");
  }

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_numa_node, 0);
  nexus.register_req_func(kAppReqType, req_handler);

  size_t num_threads = FLAGS_process_id == 0 ? FLAGS_num_server_threads
                                             : FLAGS_num_client_threads;
  std::vector<std::thread> threads(num_threads);

  for (size_t i = 0; i < num_threads; i++) {
    threads[i] = std::thread(FLAGS_process_id == 0 ? server_func : client_func,
                             &nexus, i);
    // Each worker pins itself after transport initialization.
  }

  for (size_t i = 0; i < num_threads; i++) threads[i].join();
}

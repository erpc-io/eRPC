/**
 * @file large_rpc_tput.cc
 *
 * @brief Benchmark to measure large RPC throughput. Each thread measures its
 * RX and TX bandwidth.
 *
 * Each thread creates at most session. The session connectivity is controlled
 * by the "profile" flag. The available profiles are:
 *   o incast: Incast
 *   o victim: With N processes {0, ..., N - 1}, where N >= 3:
 *     o Process 0 and process (N - 1) do not send requests
 *     o All threads on processes 1 through (N - 2) incast to process 0
 *     o Thread T - 1 on processes (N - 2) sends requests to process (N - 1)
 */

#include "large_rpc_tput.h"
#include <signal.h>
#include <cstring>
#include "profile_incast.h"
#include "profile_victim.h"
#include "util/autorun_helpers.h"

static constexpr bool kAppVerbose = false;

// Experiment control flags
static constexpr bool kAppClientMemsetReq = false;   // Fill entire request
static constexpr bool kAppServerMemsetResp = false;  // Fill entire response
static constexpr bool kAppClientCheckResp = false;   // Check entire response

std::function<void(AppContext *)> connect_sessions_func = nullptr;

// A basic session management handler that expects successful responses
void sm_handler(int session_num, erpc::SmEventType sm_event_type,
                erpc::SmErrType sm_err_type, void *_context) {
  auto *c = static_cast<AppContext *>(_context);
  c->num_sm_resps++;

  erpc::rt_assert(sm_err_type == erpc::SmErrType::kNoError,
                  "SM response with error");

  if (!(sm_event_type == erpc::SmEventType::kConnected ||
        sm_event_type == erpc::SmEventType::kDisconnected)) {
    throw std::runtime_error("Received unexpected SM event.");
  }

  // The callback gives us the eRPC session number - get the index in vector
  size_t session_idx = c->session_num_vec.size();
  for (size_t i = 0; i < c->session_num_vec.size(); i++) {
    if (c->session_num_vec[i] == session_num) {
      session_idx = i;
    }
  }

  erpc::rt_assert(session_idx < c->session_num_vec.size(),
                  "SM callback for invalid session number.");

  fprintf(stderr,
          "large_rpc_tput: Rpc %u: Session number %d (index %zu) %s. "
          "Time elapsed = %.3f s.\n",
          c->rpc->get_rpc_id(), session_num, session_idx,
          sm_event_type == erpc::SmEventType::kConnected ? "connected"
                                                         : "disconncted",
          c->rpc->sec_since_creation());
}

void app_cont_func(erpc::RespHandle *, void *, size_t);  // Forward declaration

// Send a request using this MsgBuffer
void send_req(AppContext *c, size_t msgbuf_idx) {
  erpc::MsgBuffer &req_msgbuf = c->req_msgbuf[msgbuf_idx];
  assert(req_msgbuf.get_data_size() == FLAGS_req_size);

  if (kAppVerbose) {
    printf("large_rpc_tput: Thread %zu sending request using msgbuf_idx %zu.\n",
           c->thread_id, msgbuf_idx);
  }

  // Timestamp before trying enqueue_request(). If enqueue_request() fails,
  // we'll timestamp again on the next try.
  c->req_ts[msgbuf_idx] = erpc::rdtsc();
  c->rpc->enqueue_request(c->session_num_vec[0], kAppReqType, &req_msgbuf,
                          &c->resp_msgbuf[msgbuf_idx], app_cont_func,
                          msgbuf_idx);

  c->stat_tx_bytes_tot += FLAGS_req_size;
}

void req_handler(erpc::ReqHandle *req_handle, void *_context) {
  auto *c = static_cast<AppContext *>(_context);
  const erpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  uint8_t resp_byte = req_msgbuf->buf[0];

  // Use dynamic response
  req_handle->prealloc_used = false;
  erpc::MsgBuffer &resp_msgbuf = req_handle->dyn_resp_msgbuf;
  resp_msgbuf = c->rpc->alloc_msg_buffer(FLAGS_resp_size);  // Freed by eRPC
  assert(resp_msgbuf.buf != nullptr);

  // Touch the response
  if (kAppServerMemsetResp) {
    memset(resp_msgbuf.buf, resp_byte, FLAGS_resp_size);
  } else {
    resp_msgbuf.buf[0] = resp_byte;
  }

  c->stat_rx_bytes_tot += FLAGS_req_size;
  c->stat_tx_bytes_tot += FLAGS_resp_size;

  c->rpc->enqueue_response(req_handle);

  // Print burstiness stats, twice per second
  double ns = erpc::ns_since(c->tput_t0);
  if (erpc::kDatapathStats && ns >= 500000000) {
    auto &stats = c->rpc->dpath_stats;
    printf(
        "large_rpc_tput: Thread %zu: "
        "Pkts/event loop call = {%.4f Tx, %.4f Rx}\n",
        c->thread_id, stats.pkts_tx / (stats.tx_burst_calls + 0.1),
        stats.pkts_rx / (stats.rx_burst_calls + 0.1));

    clock_gettime(CLOCK_REALTIME, &c->tput_t0);
  }
}

void app_cont_func(erpc::RespHandle *resp_handle, void *_context, size_t _tag) {
  const erpc::MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  size_t msgbuf_idx = _tag;
  if (kAppVerbose) {
    printf("large_rpc_tput: Received response for msgbuf %zu.\n", msgbuf_idx);
  }

  // Measure latency. 1 us granularity is sufficient for large RPC latency.
  auto *c = static_cast<AppContext *>(_context);
  double usec = erpc::to_usec(erpc::rdtsc() - c->req_ts[msgbuf_idx],
                              c->rpc->get_freq_ghz());
  c->latency_vec.push_back(usec);

  // Check the response
  erpc::rt_assert(resp_msgbuf->get_data_size() == FLAGS_resp_size,
                  "Invalid response size");

  if (kAppClientCheckResp) {
    bool match = true;
    // Check all response cachelines (checking every byte is slow)
    for (size_t i = 0; i < FLAGS_resp_size; i += 64) {
      if (resp_msgbuf->buf[i] != kAppDataByte) match = false;
    }
    erpc::rt_assert(match, "Invalid resp data");
  } else {
    erpc::rt_assert(resp_msgbuf->buf[0] == kAppDataByte, "Invalid resp data");
  }

  c->stat_rx_bytes_tot += FLAGS_resp_size;
  c->rpc->release_response(resp_handle);

  // Print twice every second
  double ns = erpc::ns_since(c->tput_t0);
  if (ns >= 500000000) {
    double rx_gbps = c->stat_rx_bytes_tot * 8 / ns;
    double tx_gbps = c->stat_tx_bytes_tot * 8 / ns;

    // Compute latency stats
    std::sort(c->latency_vec.begin(), c->latency_vec.end());
    double latency_sum = 0.0;
    for (double sample : c->latency_vec) latency_sum += sample;

    double avg_us = latency_sum / c->latency_vec.size();
    double _99_us = c->latency_vec.at(c->latency_vec.size() * 0.99);

    erpc::Timely *timely_0 = c->rpc->get_timely(0);

    printf(
        "large_rpc_tput: Thread %zu: Tput {RX %.2f, TX %.2f} Gbps, "
        "latency {%.1f, %.1f}, transfer {RX %.1f, TX %.1f} MB, "
        "Session 0: {{%.1f, %.1f, %.1f} us, %.2f Gbps}. Credits %zu/best 32.\n",
        c->thread_id, rx_gbps, tx_gbps, avg_us, _99_us,
        c->stat_rx_bytes_tot / 1000000.0, c->stat_tx_bytes_tot / 1000000.0,
        timely_0->get_rtt_perc(.5), timely_0->get_rtt_perc(.9),
        timely_0->get_rtt_perc(.99), timely_0->get_rate_gbps(),
        erpc::kSessionCredits);

    timely_0->reset_rtt_stats();

    // Stats: rx_gbps tx_gbps avg_us 99_us
    c->tmp_stat->write(std::to_string(rx_gbps) + " " + std::to_string(tx_gbps) +
                       " " + std::to_string(avg_us) + " " +
                       std::to_string(_99_us));

    c->latency_vec.clear();
    c->stat_rx_bytes_tot = 0;
    c->stat_tx_bytes_tot = 0;
    c->rpc->reset_dpath_stats_st();

    clock_gettime(CLOCK_REALTIME, &c->tput_t0);
  }

  // Create a new request clocking this response, and put in request queue
  if (kAppClientMemsetReq) {
    memset(c->req_msgbuf[msgbuf_idx].buf, kAppDataByte, FLAGS_req_size);
  } else {
    c->req_msgbuf[msgbuf_idx].buf[0] = kAppDataByte;
  }

  send_req(c, msgbuf_idx);
}

// The function executed by each thread in the cluster
void thread_func(size_t thread_id, erpc::Nexus *nexus) {
  AppContext c;
  c.tmp_stat = new TmpStat("rx_gbps tx_gbps avg_us 99_us");
  c.thread_id = thread_id;

  std::vector<size_t> port_vec = flags_get_numa_ports(FLAGS_numa_node);
  erpc::rt_assert(port_vec.size() > 0);
  uint8_t phy_port = port_vec.at(thread_id % port_vec.size());

  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c),
                                  static_cast<uint8_t>(thread_id), sm_handler,
                                  phy_port);
  rpc.retry_connect_on_invalid_rpc_id = true;
  if (erpc::kTesting) rpc.fault_inject_set_pkt_drop_prob_st(FLAGS_drop_prob);

  c.rpc = &rpc;

  // Create the session. Some threads may not create any sessions, and therefore
  // not run the event loop required for other threads to connect them. This
  // is OK because all threads will run the event loop below.
  connect_sessions_func(&c);

  if (c.session_num_vec.size() > 0) {
    printf("large_rpc_tput: Thread %zu: All sessions connected.\n", thread_id);
  } else {
    printf("large_rpc_tput: Thread %zu: No sessions created.\n", thread_id);
  }

  // Regardless of the profile and thread role, all threads allocate request
  // and response MsgBuffers. Some threads may not send requests.
  alloc_req_resp_msg_buffers(&c);

  clock_gettime(CLOCK_REALTIME, &c.tput_t0);

  // Send requests. For some profiles, process 0 does not send requests.
  // In these cases, by not injecting any requests now, we ensure that process 0
  // *never* sends requests.
  bool _send_reqs = true;
  if (FLAGS_process_id == 0) {
    if (FLAGS_profile == "incast" || FLAGS_profile == "victim") {
      _send_reqs = false;
    }
  }

  if (_send_reqs) {
    erpc::rt_assert(c.session_num_vec.size() > 0,
                    "Cannot send requests without sessions");

    for (size_t msgbuf_idx = 0; msgbuf_idx < FLAGS_concurrency; msgbuf_idx++) {
      send_req(&c, msgbuf_idx);
    }
  }

  for (size_t i = 0; i < FLAGS_test_ms; i += 1000) {
    rpc.run_event_loop(1000);  // 1 second
    if (ctrl_c_pressed == 1) break;
  }

  // We don't disconnect sessions
}

// Use the supplied profile set up globals and possibly modify other flags
void setup_profile() {
  if (FLAGS_profile == "incast") {
    connect_sessions_func = connect_sessions_func_incast;
    return;
  }

  if (FLAGS_profile == "victim") {
    erpc::rt_assert(FLAGS_num_processes >= 3, "Too few processes");
    erpc::rt_assert(FLAGS_num_threads >= 2, "Too few threads");
    connect_sessions_func = connect_sessions_func_victim;
    return;
  }
}

int main(int argc, char **argv) {
  signal(SIGINT, ctrl_c_handler);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  erpc::rt_assert(FLAGS_concurrency <= kAppMaxConcurrency, "Invalid conc");
  erpc::rt_assert(FLAGS_profile == "incast" || FLAGS_profile == "victim",
                  "Invalid profile");
  if (!erpc::kTesting) {
    erpc::rt_assert(FLAGS_drop_prob == 0.0, "Invalid drop prob");
  } else {
    erpc::rt_assert(FLAGS_drop_prob < 1, "Invalid drop prob");
  }

  setup_profile();
  erpc::rt_assert(connect_sessions_func != nullptr, "No connect_sessions_func");

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_numa_node, 0);
  nexus.register_req_func(
      kAppReqType, erpc::ReqFunc(req_handler, erpc::ReqFuncType::kForeground));

  std::vector<std::thread> threads(FLAGS_num_threads);
  for (size_t i = 0; i < FLAGS_num_threads; i++) {
    threads[i] = std::thread(thread_func, i, &nexus);
    erpc::bind_to_core(threads[i], FLAGS_numa_node, i);
  }

  for (auto &thread : threads) thread.join();
}

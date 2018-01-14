/**
 * @file large_rpc_tput.cc
 *
 * @brief Benchmark to measure large RPC throughput. Each thread measures its
 * response RX and TX bandwidth. For this measurement to be useful, request
 * size should be small and response size large.
 *
 * A request is described by its MsgBuffer index and session index, and is
 * queued into req_vec until it can be transmitted. Before queueing a request
 * descriptor, it's request MsgBuffer must be filled with request data.
 *
 * The experiment configuration is controlled by the "profile" flag. The profile
 * setting can override other flags such as request and response size. The
 * available profiles are:
 *   o random: Each thread sends requests and responses to randomly chosen
 *     threads, excluding itself.
 *   o incast: The small-scale incast experiment in TIMELY
 *     (SIGCOMM 15, Section 6.1).
 *   o victim: With N processes {0, ..., N - 1}, where N >= 3, processes 1
 *     through (N - 1) incast to process 0. In addition, processes (N - 2) and
 *     (N - 1) send data to each other.
 */

#include "large_rpc_tput.h"
#include <signal.h>
#include <cstring>
#include "profile_incast.h"
#include "profile_random.h"
#include "profile_victim.h"
#include "util/autorun_helpers.h"

static constexpr bool kAppVerbose = false;

// Experiment control flags
static constexpr bool kAppClientMemsetReq = false;   // Fill entire request
static constexpr bool kAppServerMemsetResp = false;  // Fill entire response
static constexpr bool kAppClientCheckResp = false;   // Check entire response

// Profile controls
std::function<size_t(AppContext *, size_t resp_session_idx)>
    get_session_idx_func = nullptr;
std::function<void(AppContext *)> connect_sessions_func = nullptr;

bool is_papi_usable = false;  // Not usable on Ubuntu 17.04

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

// Send requests (i.e., msgbuf indexes) queued in req_vec. Requests that cannot
// be sent are req-queued into req_vec.
void send_reqs(AppContext *c) {
  size_t write_index = 0;  // XXX: This is unused

  for (size_t i = 0; i < c->req_vec.size(); i++) {
    size_t msgbuf_idx = c->req_vec[i].s.msgbuf_idx;
    size_t session_idx = c->req_vec[i].s.session_idx;

    erpc::MsgBuffer &req_msgbuf = c->req_msgbuf[msgbuf_idx];
    assert(req_msgbuf.get_data_size() == FLAGS_req_size);

    if (kAppVerbose) {
      printf(
          "large_rpc_tput: Trying to send request for session index %zu, "
          "msgbuf_idx %zu.\n",
          session_idx, msgbuf_idx);
    }

    // Timestamp before trying enqueue_request(). If enqueue_request() fails,
    // we'll timestamp again on the next try.
    c->req_ts[msgbuf_idx] = erpc::rdtsc();
    c->rpc->enqueue_request(c->session_num_vec[session_idx], kAppReqType,
                            &req_msgbuf, &c->resp_msgbuf[msgbuf_idx],
                            app_cont_func, c->req_vec[i]._tag);

    c->stat_req_vec[session_idx]++;
    c->stat_tx_bytes_tot += FLAGS_req_size;
  }

  c->req_vec.resize(write_index);  // Pending requests = write_index
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
}

void app_cont_func(erpc::RespHandle *resp_handle, void *_context, size_t _tag) {
  const erpc::MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  size_t msgbuf_idx = static_cast<tag_t>(_tag).s.msgbuf_idx;
  size_t session_idx = static_cast<tag_t>(_tag).s.session_idx;
  if (kAppVerbose) {
    printf("large_rpc_tput: Received response for msgbuf %zu, session %zu.\n",
           msgbuf_idx, session_idx);
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

  if (c->stat_rx_bytes_tot >= 50000000 || c->stat_tx_bytes_tot >= 50000000) {
    float ipc = -1.0;
    if (FLAGS_num_threads == 1 && is_papi_usable) ipc = papi_get_ipc();

    double ns = erpc::ns_since(c->tput_t0);
    double rx_gbps = c->stat_rx_bytes_tot * 8 / ns;
    double tx_gbps = c->stat_tx_bytes_tot * 8 / ns;

    // Compute latency stats
    std::sort(c->latency_vec.begin(), c->latency_vec.end());
    double latency_sum = 0.0;
    for (double sample : c->latency_vec) latency_sum += sample;

    double avg_us = latency_sum / c->latency_vec.size();
    double _99_us = c->latency_vec.at(c->latency_vec.size() * 0.99);

    std::string session_req_count_str;
    for (size_t session_req_count : c->stat_req_vec) {
      session_req_count_str += std::to_string(session_req_count);
      session_req_count_str += " ";
    }

    printf(
        "large_rpc_tput: Thread %zu: Response tput: RX %.3f Gbps, "
        "TX %.3f Gbps, avg latency = %.1f us, 99%% latency = %.1f us. "
        "RX = %.3f MB, TX = %.3f MB. IPC = %.3f. Requests on sessions = %s.\n",
        c->thread_id, rx_gbps, tx_gbps, avg_us, _99_us,
        c->stat_rx_bytes_tot / 1000000.0, c->stat_tx_bytes_tot / 1000000.0, ipc,
        session_req_count_str.c_str());

    // Stats: rx_gbps tx_gbps avg_us 99_us
    c->tmp_stat->write(std::to_string(rx_gbps) + " " + std::to_string(tx_gbps) +
                       " " + std::to_string(avg_us) + " " +
                       std::to_string(_99_us));

    c->latency_vec.clear();
    c->stat_rx_bytes_tot = 0;
    c->stat_tx_bytes_tot = 0;
    c->rpc->reset_dpath_stats_st();
    std::fill(c->stat_req_vec.begin(), c->stat_req_vec.end(), 0);

    clock_gettime(CLOCK_REALTIME, &c->tput_t0);
  }

  // Create a new request clocking this response, and put in request queue
  if (kAppClientMemsetReq) {
    memset(c->req_msgbuf[msgbuf_idx].buf, kAppDataByte, FLAGS_req_size);
  } else {
    c->req_msgbuf[msgbuf_idx].buf[0] = kAppDataByte;
  }

  // For some profiles, the session_idx argument will be ignored
  c->req_vec.push_back(tag_t(get_session_idx_func(c, session_idx), msgbuf_idx));

  // Try to send the queued requests. The request buffer for these requests is
  // already filled.
  send_reqs(c);
}

// The function executed by each thread in the cluster
void thread_func(size_t thread_id, erpc::Nexus *nexus) {
  AppContext c;
  c.tmp_stat = new TmpStat("rx_gbps tx_gbps avg_us 99_us");
  c.thread_id = thread_id;

  uint8_t phy_port;
  if (FLAGS_numa_node == 0) phy_port = numa_0_ports[thread_id % 2];
  if (FLAGS_numa_node == 1) phy_port = numa_1_ports[thread_id % 2];
  erpc::Rpc<erpc::CTransport> rpc(nexus, static_cast<void *>(&c),
                                  static_cast<uint8_t>(thread_id), sm_handler,
                                  phy_port);
  rpc.retry_connect_on_invalid_rpc_id = true;
  if (erpc::kTesting) rpc.fault_inject_set_pkt_drop_prob_st(FLAGS_drop_prob);

  c.rpc = &rpc;

  // Create sessions. Some threads may not create any sessions, and therefore
  // not run the event loop required for other threads to connect them. This
  // is OK because all threads will run the event loop below.
  connect_sessions_func(&c);

  if (c.session_num_vec.size() > 0) {
    fprintf(stderr, "large_rpc_tput: Thread %zu: All sessions connected.\n",
            thread_id);
    c.stat_req_vec.resize(c.session_num_vec.size());
    std::fill(c.stat_req_vec.begin(), c.stat_req_vec.end(), 0);
  } else {
    fprintf(stderr, "large_rpc_tput: Thread %zu: No sessions created.\n",
            thread_id);
  }

  // Regardless of the profile and thread role, all threads allocate request
  // and response MsgBuffers. Some threads may not send requests.
  alloc_req_resp_msg_buffers(&c);

  if (FLAGS_num_threads == 1) {
    is_papi_usable = papi_init();  // No IPC for multi-threaded
  }

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
      size_t session_idx = get_session_idx_func(&c, SIZE_MAX);
      c.req_vec.push_back(tag_t(session_idx, msgbuf_idx));
    }
    send_reqs(&c);
  }

  for (size_t i = 0; i < FLAGS_test_ms; i += 1000) {
    rpc.run_event_loop(1000);  // 1 second
    if (ctrl_c_pressed == 1) break;
  }

  // We don't disconnect sessions
}

// Use the supplied profile set up globals and possibly modify other flags
void setup_profile() {
  if (FLAGS_profile == "random") {
    connect_sessions_func = connect_sessions_func_random;
    get_session_idx_func = get_session_idx_func_random;
    return;
  }

  if (FLAGS_profile == "incast") {
    connect_sessions_func = connect_sessions_func_incast;
    get_session_idx_func = get_session_idx_func_incast;
    return;
  }

  if (FLAGS_profile == "victim") {
    erpc::rt_assert(FLAGS_num_processes >= 3,
                    "victim profile needs 3 or more processes.");
    erpc::rt_assert(FLAGS_concurrency >= 2,
                    "victim profile needs concurrency >= 2.");
    connect_sessions_func = connect_sessions_func_victim;
    get_session_idx_func = get_session_idx_func_victim;
    return;
  }
}

int main(int argc, char **argv) {
  signal(SIGINT, ctrl_c_handler);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  erpc::rt_assert(FLAGS_concurrency <= kAppMaxConcurrency, "Invalid conc");
  erpc::rt_assert(FLAGS_profile == "random" || FLAGS_profile == "incast" ||
                      FLAGS_profile == "victim",
                  "Invalid profile");
  if (!erpc::kTesting) {
    erpc::rt_assert(FLAGS_drop_prob == 0.0, "Invalid drop prob");
  } else {
    erpc::rt_assert(FLAGS_drop_prob < 1, "Invalid drop prob");
  }

  setup_profile();
  erpc::rt_assert(get_session_idx_func != nullptr, "No session index getter");
  erpc::rt_assert(connect_sessions_func != nullptr, "No connect_sessions_func");

  erpc::Nexus nexus(erpc::get_uri_for_process(FLAGS_process_id),
                    FLAGS_process_id, FLAGS_numa_node, 0);
  nexus.register_req_func(
      kAppReqType, erpc::ReqFunc(req_handler, erpc::ReqFuncType::kForeground));

  std::vector<std::thread> threads(FLAGS_num_threads);
  for (size_t i = 0; i < FLAGS_num_threads; i++) {
    threads[i] = std::thread(thread_func, i, &nexus);
    erpc::bind_to_core(threads[i], FLAGS_numa_node, i);
  }

  for (auto &thread : threads) thread.join();
}

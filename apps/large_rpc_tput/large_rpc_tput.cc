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
 *   o timely_small: The small-scale experiment in TIMELY
 *     (SIGCOMM 15, Section 6.1).
 */

#include "large_rpc_tput.h"
#include <signal.h>
#include <cstring>
#include "profile_random.h"
#include "profile_timely_small.h"

static constexpr bool kAppVerbose = false;

// If true, we memset() request and respose buffers to kAppDataByte. If false,
// only the first data byte is touched.
static constexpr bool kAppMemset = true;

// Profile controls
std::function<size_t(AppContext *)> get_session_idx_func = nullptr;
std::function<void(AppContext *)> connect_sessions_func = nullptr;

// A basic session management handler that expects successful responses
void sm_handler(int session_num, ERpc::SmEventType sm_event_type,
                ERpc::SmErrType sm_err_type, void *_context) {
  assert(_context != nullptr);

  auto *c = static_cast<AppContext *>(_context);
  c->num_sm_resps++;

  if (sm_err_type != ERpc::SmErrType::kNoError) {
    throw std::runtime_error("Received SM response with error.");
  }

  if (!(sm_event_type == ERpc::SmEventType::kConnected ||
        sm_event_type == ERpc::SmEventType::kDisconnected)) {
    throw std::runtime_error("Received unexpected SM event.");
  }

  // The callback gives us the ERpc session number - get the index in vector
  size_t session_idx = c->session_num_vec.size();
  for (size_t i = 0; i < c->session_num_vec.size(); i++) {
    if (c->session_num_vec[i] == session_num) {
      session_idx = i;
    }
  }

  if (session_idx == c->session_num_vec.size()) {
    throw std::runtime_error("SM callback for invalid session number.");
  }

  fprintf(stderr,
          "large_rpc_tput: Rpc %u: Session number %d (index %zu) %s. "
          "Time elapsed = %.3f s.\n",
          c->rpc->get_rpc_id(), session_num, session_idx,
          sm_event_type == ERpc::SmEventType::kConnected ? "connected"
                                                         : "disconncted",
          c->rpc->sec_since_creation());
}

void app_cont_func(ERpc::RespHandle *, void *, size_t);  // Forward declaration

// Send requests (i.e., msgbuf indexes) queued in req_vec. Requests that cannot
// be sent are req-queued into req_vec.
void send_reqs(AppContext *c) {
  assert(c != nullptr);
  size_t write_index = 0;

  for (size_t i = 0; i < c->req_vec.size(); i++) {
    size_t msgbuf_idx = c->req_vec[i].msgbuf_idx;
    size_t session_idx = c->req_vec[i].session_idx;

    ERpc::MsgBuffer &req_msgbuf = c->req_msgbuf[msgbuf_idx];
    assert(req_msgbuf.get_data_size() == FLAGS_req_size);

    if (kAppVerbose) {
      printf(
          "large_rpc_tput: Trying to send request for session index %zu, "
          "msgbuf_idx %zu.\n",
          session_idx, msgbuf_idx);
    }

    // Timestamp before trying enqueue_request(). If enqueue_request() fails,
    // we'll timestamp again on the next try.
    c->req_ts[msgbuf_idx] = ERpc::rdtsc();
    int ret = c->rpc->enqueue_request(
        c->session_num_vec[session_idx], kAppReqType, &req_msgbuf,
        &c->resp_msgbuf[msgbuf_idx], app_cont_func, c->req_vec[i]._tag);
    assert(ret == 0 || ret == -EBUSY);

    if (ret == -EBUSY) {
      c->req_vec[write_index] = c->req_vec[i];
      write_index++;
      // Try other requests
    }
  }

  c->req_vec.resize(write_index);  // Pending requests = write_index
}

void req_handler(ERpc::ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr);
  assert(_context != nullptr);

  auto *c = static_cast<AppContext *>(_context);

  const ERpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  uint8_t resp_byte = req_msgbuf->buf[0];

  // Use dynamic response
  req_handle->prealloc_used = false;
  ERpc::MsgBuffer &resp_msgbuf = req_handle->dyn_resp_msgbuf;
  resp_msgbuf = c->rpc->alloc_msg_buffer(FLAGS_resp_size);  // Freed by eRPC
  assert(resp_msgbuf.buf != nullptr);

  // Touch the response
  if (kAppMemset) {
    memset(resp_msgbuf.buf, resp_byte, FLAGS_resp_size);
  } else {
    resp_msgbuf.buf[0] = resp_byte;
  }

  c->stat_resp_tx_bytes_tot += FLAGS_resp_size;
  c->rpc->enqueue_response(req_handle);
}

void app_cont_func(ERpc::RespHandle *resp_handle, void *_context, size_t _tag) {
  assert(resp_handle != nullptr);
  assert(_context != nullptr);

  const ERpc::MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  assert(resp_msgbuf != nullptr);

  size_t msgbuf_idx = static_cast<tag_t>(_tag).msgbuf_idx;
  size_t session_idx = static_cast<tag_t>(_tag).session_idx;
  if (kAppVerbose) {
    printf("large_rpc_tput: Received response for msgbuf %zu, session %zu.\n",
           msgbuf_idx, session_idx);
  }

  // Measure latency. 1 us granularity is sufficient for large RPC latency.
  auto *c = static_cast<AppContext *>(_context);
  double usec = ERpc::to_usec(ERpc::rdtsc() - c->req_ts[msgbuf_idx],
                              c->rpc->get_freq_ghz());
  assert(usec >= 0);
  c->latency.update(static_cast<size_t>(usec));

  // Check the response
  if (unlikely(resp_msgbuf->get_data_size() != FLAGS_resp_size)) {
    throw std::runtime_error("Invalid response size.\n");
  }

  if (kAppMemset) {
    // Check all response cachelines (checking every byte is slow)
    for (size_t i = 0; i < FLAGS_resp_size; i += 64) {
      if (unlikely(resp_msgbuf->buf[i] != kAppDataByte)) {
        throw std::runtime_error("Invalid response data.");
      }
    }
  } else {
    if (unlikely(resp_msgbuf->buf[0] != kAppDataByte)) {
      throw std::runtime_error("Invalid response data.");
    }
  }

  c->stat_resp_rx_bytes_tot += FLAGS_resp_size;
  c->stat_resp_rx_bytes[session_idx] += FLAGS_resp_size;
  c->rpc->release_response(resp_handle);

  if (c->stat_resp_rx_bytes_tot == 500000000) {
    double ns = ERpc::ns_since(c->tput_t0);
    double session_max_tput = 0;
    double session_min_tput = std::numeric_limits<double>::max();

    for (size_t i = 0; i < c->session_num_vec.size(); i++) {
      if (i == c->self_session_idx) continue;
      session_max_tput =
          std::max(c->stat_resp_rx_bytes[i] / ns, session_max_tput);
      session_min_tput =
          std::min(c->stat_resp_rx_bytes[i] / ns, session_min_tput);
    }

    float ipc = -1.0;
    if (FLAGS_num_threads == 1) {
      float real_time, proc_time;
      long long ins;
      int ret = PAPI_ipc(&real_time, &proc_time, &ins, &ipc);
      if (ret < PAPI_OK) throw std::runtime_error("PAPI measurement failed.");
    }

    printf(
        "large_rpc_tput: Thread %zu: Response tput: RX %.3f GB/s, "
        "TX %.3f GB/s. Response bytes: RX %.3f MB, TX = %.3f MB. "
        "Max,min session tput = %.3f GB/s, %.3f GB/s. IPC = %.3f.\n",
        c->thread_id, c->stat_resp_rx_bytes_tot / ns,
        c->stat_resp_tx_bytes_tot / ns, c->stat_resp_rx_bytes_tot / 1000000.0,
        c->stat_resp_tx_bytes_tot / 1000000.0, session_max_tput,
        session_min_tput, ipc);

    // Stats: rx_GBps tx_GBps avg_us 99_us
    c->tmp_stat->write(std::to_string(c->stat_resp_rx_bytes_tot / ns) + " " +
                       std::to_string(c->stat_resp_tx_bytes_tot / ns) + " " +
                       std::to_string(c->latency.avg()) + " " +
                       std::to_string(c->latency.perc(.99)));

    c->latency.reset();
    std::fill(c->stat_resp_rx_bytes.begin(), c->stat_resp_rx_bytes.end(), 0);
    c->stat_resp_rx_bytes_tot = 0;
    c->stat_resp_tx_bytes_tot = 0;
    c->rpc->reset_dpath_stats_st();

    clock_gettime(CLOCK_REALTIME, &c->tput_t0);
  }

  // Create a new request clocking this response, and put in request queue
  if (kAppMemset) {
    memset(c->req_msgbuf[msgbuf_idx].buf, kAppDataByte, FLAGS_req_size);
  } else {
    c->req_msgbuf[msgbuf_idx].buf[0] = kAppDataByte;
  }

  c->req_vec.push_back(tag_t(get_session_idx_func(c), msgbuf_idx));

  // Try to send the queued requests. The request buffer for these requests is
  // already filled.
  send_reqs(c);
}

// The function executed by each thread in the cluster
void thread_func(size_t thread_id, ERpc::Nexus<ERpc::IBTransport> *nexus) {
  AppContext c;
  c.tmp_stat =
      new ERpc::TmpStat("large_rpc_tput", "rx_GBps tx_GBps avg_us 99_us");
  c.thread_id = thread_id;
  c.self_session_idx = FLAGS_machine_id * FLAGS_num_threads + thread_id;

  ERpc::Rpc<ERpc::IBTransport> rpc(nexus, static_cast<void *>(&c),
                                   static_cast<uint8_t>(thread_id), sm_handler,
                                   kAppPhyPort, kAppNumaNode);
  rpc.retry_connect_on_invalid_rpc_id = true;
  c.rpc = &rpc;
  connect_sessions_func(&c);

  fprintf(stderr, "large_rpc_tput: Thread %zu: All sessions connected.\n",
          thread_id);

  // Regardless of the profile and thread role, all threads allocate request
  // and response MsgBuffers
  for (size_t msgbuf_idx = 0; msgbuf_idx < FLAGS_concurrency; msgbuf_idx++) {
    // Allocate request and response MsgBuffers
    c.resp_msgbuf[msgbuf_idx] = rpc.alloc_msg_buffer(FLAGS_resp_size);
    if (c.resp_msgbuf[msgbuf_idx].buf == nullptr) {
      throw std::runtime_error("Failed to pre-allocate response MsgBuffer.");
    }

    auto &req_msgbuf = c.req_msgbuf[msgbuf_idx];
    req_msgbuf = rpc.alloc_msg_buffer(FLAGS_req_size);
    if (req_msgbuf.buf == nullptr) {
      throw std::runtime_error("Failed to pre-allocate req MsgBuffer.");
    }

    // Fill request and enqueue it
    if (kAppMemset) {
      memset(req_msgbuf.buf, kAppDataByte, FLAGS_req_size);
    } else {
      req_msgbuf.buf[0] = kAppDataByte;
    }

    c.req_vec.push_back(tag_t(get_session_idx_func(&c), msgbuf_idx));
  }

  // Initialize PAPI measurement if we're running one thread
  if (FLAGS_num_threads == 1) {
    float real_time, proc_time, ipc;
    long long ins;
    int ret = PAPI_ipc(&real_time, &proc_time, &ins, &ipc);
    if (ret < PAPI_OK) throw std::runtime_error("PAPI initialization failed.");
  }

  clock_gettime(CLOCK_REALTIME, &c.tput_t0);

  // Send queued requests
  send_reqs(&c);

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

  if (FLAGS_profile == "timely_small") {
    FLAGS_req_size = 64 * 1024;
    FLAGS_resp_size = 32;
    connect_sessions_func = connect_sessions_func_timely_small;
    get_session_idx_func = get_session_idx_func_timely_small;
    return;
  }
}

int main(int argc, char **argv) {
  assert(FLAGS_num_bg_threads == 0);  // XXX: Need to change ReqFuncType below
  signal(SIGINT, ctrl_c_handler);

  if (!ERpc::large_rpc_supported()) {
    throw std::runtime_error(
        "Current eRPC optlevel does not allow large RPCs.");
  }

  // Work around g++-5's unused variable warning for validators
  _unused(concurrency_validator_registered);
  _unused(profile_validator_registered);

  // Parse args
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  setup_profile();
  if (get_session_idx_func == nullptr) {
    throw std::runtime_error("Profile must set session index getter.");
  }
  if (connect_sessions_func == nullptr) {
    throw std::runtime_error("Profile must set connect sessions function.");
  }

  std::string machine_name = get_hostname_for_machine(FLAGS_machine_id);
  ERpc::Nexus<ERpc::IBTransport> nexus(machine_name, kAppNexusUdpPort,
                                       FLAGS_num_bg_threads);
  nexus.register_req_func(
      kAppReqType, ERpc::ReqFunc(req_handler, ERpc::ReqFuncType::kForeground));

  std::thread threads[FLAGS_num_threads];
  for (size_t i = 0; i < FLAGS_num_threads; i++) {
    threads[i] = std::thread(thread_func, i, &nexus);
    ERpc::bind_to_core(threads[i], i);
  }

  for (size_t i = 0; i < FLAGS_num_threads; i++) {
    threads[i].join();
  }
}

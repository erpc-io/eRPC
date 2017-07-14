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
 */

#include <gflags/gflags.h>
#include <papi.h>
#include <signal.h>
#include <cstring>
#include "../apps_common.h"
#include "rpc.h"
#include "util/latency.h"
#include "util/misc.h"

static constexpr bool kAppVerbose = false;

// If true, we memset() request and respose buffers to kAppDataByte. If false,
// only the first data byte is touched.
static constexpr bool kAppMemset = true;

static constexpr size_t kAppNexusUdpPort = 31851;
static constexpr size_t kAppPhyPort = 0;
static constexpr size_t kAppNumaNode = 0;
static constexpr size_t kAppReqType = 1;
static constexpr uint8_t kAppDataByte = 3;  // Data transferred in req & resp
static constexpr size_t kMaxConcurrency = 32;

DEFINE_uint64(num_threads, 0, "Number of foreground threads per machine");
DEFINE_uint64(num_bg_threads, 0, "Number of background threads per machine");
DEFINE_uint64(req_size, 0, "Request data size");
DEFINE_uint64(resp_size, 0, "Response data size");
DEFINE_uint64(concurrency, 0, "Concurrent batches per thread");

static bool validate_concurrency(const char *, uint64_t concurrency) {
  return concurrency <= kMaxConcurrency;
}

DEFINE_validator(concurrency, &validate_concurrency);

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

// Return the control net IP address of the machine with index server_i
static std::string get_hostname_for_machine(size_t server_i) {
  std::ostringstream ret;
  ret << "3.1.8." << std::to_string(server_i + 1);
  // ret << std::string("akalianode-") << std::to_string(server_i + 1)
  //    << std::string(".RDMA.fawn.apt.emulab.net");
  return ret.str();
}

// Request tag, which doubles up as the request descriptor for the request queue
union tag_t {
  struct {
    uint64_t session_index : 32;  // Index into context's session_num array
    uint64_t msgbuf_index : 32;   // Index into context's req_msgbuf array
  };

  size_t _tag;

  tag_t(uint64_t session_index, uint64_t msgbuf_index)
      : session_index(session_index), msgbuf_index(msgbuf_index) {}
  tag_t(size_t _tag) : _tag(_tag) {}
  tag_t() : _tag(0) {}
};

static_assert(sizeof(tag_t) == sizeof(size_t), "");

// Per-thread application context
class AppContext {
 public:
  ERpc::Rpc<ERpc::IBTransport> *rpc = nullptr;
  ERpc::TmpStat *tmp_stat = nullptr;
  ERpc::Latency latency;

  std::vector<int> session_num_vec;

  // The entry in session_arr for this thread, so we don't send reqs to ourself
  size_t self_session_index;
  size_t thread_id;         // The ID of the thread that owns this context
  size_t num_sm_resps = 0;  // Number of SM responses
  struct timespec tput_t0;  // Start time for throughput measurement
  ERpc::FastRand fastrand;

  size_t stat_resp_rx_bytes_tot = 0;       // Total response bytes received
  size_t stat_resp_tx_bytes_tot = 0;       // Total response bytes transmitted
  std::vector<size_t> stat_resp_rx_bytes;  // Resp bytes received on a session

  std::vector<tag_t> req_vec;  // Request queue

  uint64_t req_ts[kMaxConcurrency];  // Per-request timestamps
  ERpc::MsgBuffer req_msgbuf[kMaxConcurrency];
  ERpc::MsgBuffer resp_msgbuf[kMaxConcurrency];

  ~AppContext() {
    if (tmp_stat != nullptr) delete tmp_stat;
  }
};

// A basic session management handler that expects successful responses
void basic_sm_handler(int session_num, ERpc::SmEventType sm_event_type,
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
  size_t session_index = c->session_num_vec.size();
  for (size_t i = 0; i < c->session_num_vec.size(); i++) {
    if (c->session_num_vec[i] == session_num) {
      session_index = i;
    }
  }

  if (session_index == c->session_num_vec.size()) {
    throw std::runtime_error("SM callback for invalid session number.");
  }

  fprintf(stderr,
          "large_rpc_tput: Rpc %u: Session number %d (index %zu) %s. "
          "Time elapsed = %.3f s.\n",
          c->rpc->get_rpc_id(), session_num, session_index,
          sm_event_type == ERpc::SmEventType::kConnected ? "connected"
                                                         : "disconncted",
          c->rpc->sec_since_creation());
}

size_t get_rand_session_index(AppContext *c) {
  assert(c != nullptr);
  size_t num_sessions = c->session_num_vec.size();

  // Use Lemire's trick to compute random numbers modulo c->num_sessions
  uint32_t x = c->fastrand.next_u32();
  size_t rand_session_index = (static_cast<size_t>(x) * num_sessions) >> 32;
  while (rand_session_index == c->self_session_index) {
    x = c->fastrand.next_u32();
    rand_session_index = (static_cast<size_t>(x) * num_sessions) >> 32;
  }

  return rand_session_index;
}

void app_cont_func(ERpc::RespHandle *, void *, size_t);  // Forward declaration

// Send requests (i.e., msgbuf indexes) queued in req_vec. Requests that cannot
// be sent are req-queued into req_vec.
void send_reqs(AppContext *c) {
  assert(c != nullptr);

  size_t write_index = 0;
  for (size_t i = 0; i < c->req_vec.size(); i++) {
    size_t msgbuf_index = c->req_vec[i].msgbuf_index;
    size_t session_index = c->req_vec[i].session_index;

    ERpc::MsgBuffer &req_msgbuf = c->req_msgbuf[msgbuf_index];
    assert(req_msgbuf.get_data_size() == FLAGS_req_size);

    if (kAppVerbose) {
      printf("large_rpc_tput: Sending req for session %zu, msgbuf_index %zu.\n",
             session_index, msgbuf_index);
    }

    c->req_ts[msgbuf_index] = ERpc::rdtsc();
    int ret = c->rpc->enqueue_request(
        c->session_num_vec[session_index], kAppReqType, &req_msgbuf,
        &c->resp_msgbuf[msgbuf_index], app_cont_func, c->req_vec[i]._tag);
    assert(ret == 0 || ret == -EBUSY);

    if (ret == -EBUSY) {
      c->req_vec[write_index] = c->req_vec[i];
      write_index++;
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

  size_t msgbuf_index = static_cast<tag_t>(_tag).msgbuf_index;
  size_t session_index = static_cast<tag_t>(_tag).session_index;
  if (kAppVerbose) {
    printf("large_rpc_tput: Received response for msgbuf %zu, session %zu.\n",
           msgbuf_index, session_index);
  }

  // Measure latency. 1 us granularity is sufficient for large RPC latency.
  auto *c = static_cast<AppContext *>(_context);
  double usec = ERpc::to_usec(ERpc::rdtsc() - c->req_ts[msgbuf_index],
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

  // Create a new request clocking this response, and put in request queue
  if (kAppMemset) {
    memset(c->req_msgbuf[msgbuf_index].buf, kAppDataByte, FLAGS_req_size);
  } else {
    c->req_msgbuf[msgbuf_index].buf[0] = kAppDataByte;
  }

  c->req_vec.push_back(tag_t(get_rand_session_index(c), msgbuf_index));

  // Try to send the queued requests. The request buffer for these requests is
  // already filled.
  send_reqs(c);

  c->stat_resp_rx_bytes_tot += FLAGS_resp_size;
  c->stat_resp_rx_bytes[session_index] += FLAGS_resp_size;

  if (c->stat_resp_rx_bytes_tot == 500000000) {
    double ns = ERpc::ns_since(c->tput_t0);
    double session_max_tput = 0;
    double session_min_tput = std::numeric_limits<double>::max();

    for (size_t i = 0; i < c->session_num_vec.size(); i++) {
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

    // Stats: throughput ipc avg_latency 99%_latency
    c->tmp_stat->write(std::to_string(c->stat_resp_rx_bytes_tot / ns) + " " +
                       std::to_string(ipc) + " " +
                       std::to_string(c->latency.avg()) + " " +
                       std::to_string(c->latency.perc(.99)));

    c->rpc->reset_dpath_stats_st();
    std::fill(c->stat_resp_rx_bytes.begin(), c->stat_resp_rx_bytes.end(), 0);
    c->stat_resp_rx_bytes_tot = 0;
    c->stat_resp_tx_bytes_tot = 0;

    clock_gettime(CLOCK_REALTIME, &c->tput_t0);
  }

  c->rpc->release_response(resp_handle);
}

// The function executed by each thread in the cluster
void thread_func(size_t thread_id, ERpc::Nexus<ERpc::IBTransport> *nexus) {
  AppContext c;
  c.tmp_stat =
      new ERpc::TmpStat("large_rpc_tput", "rx_GBps tx_GBps avg_ms 99_ms");
  c.thread_id = thread_id;
  c.self_session_index = FLAGS_machine_id * FLAGS_num_threads + thread_id;

  ERpc::Rpc<ERpc::IBTransport> rpc(nexus, static_cast<void *>(&c),
                                   static_cast<uint8_t>(thread_id),
                                   basic_sm_handler, kAppPhyPort, kAppNumaNode);
  rpc.retry_connect_on_invalid_rpc_id = true;
  c.rpc = &rpc;

  // Allocate per-session info
  size_t num_sessions = FLAGS_num_machines * FLAGS_num_threads;
  c.session_num_vec.resize(num_sessions);
  std::fill(c.session_num_vec.begin(), c.session_num_vec.end(), -1);

  c.stat_resp_rx_bytes.resize(num_sessions);
  std::fill(c.stat_resp_rx_bytes.begin(), c.stat_resp_rx_bytes.end(), 0);

  // Initiate connection for sessions
  fprintf(stderr, "large_rpc_tput: Thread %zu: Creating sessions.\n",
          thread_id);
  for (size_t m_i = 0; m_i < FLAGS_num_machines; m_i++) {
    std::string hostname = get_hostname_for_machine(m_i);

    for (size_t t_i = 0; t_i < FLAGS_num_threads; t_i++) {
      size_t session_index = (m_i * FLAGS_num_threads) + t_i;
      // Do not create a session to self
      if (session_index == c.self_session_index) continue;

      c.session_num_vec[session_index] =
          rpc.create_session(hostname, static_cast<uint8_t>(t_i), kAppPhyPort);
      assert(c.session_num_vec[session_index] >= 0);
    }
  }

  while (c.num_sm_resps != FLAGS_num_machines * FLAGS_num_threads - 1) {
    rpc.run_event_loop(200);  // 200 milliseconds
    if (ctrl_c_pressed == 1) return;
  }

  fprintf(stderr, "large_rpc_tput: Thread %zu: All sessions connected.\n",
          thread_id);

  for (size_t msgbuf_index = 0; msgbuf_index < FLAGS_concurrency;
       msgbuf_index++) {
    // Allocate request and response MsgBuffers
    c.resp_msgbuf[msgbuf_index] = rpc.alloc_msg_buffer(FLAGS_resp_size);
    if (c.resp_msgbuf[msgbuf_index].buf == nullptr) {
      throw std::runtime_error("Failed to pre-allocate response MsgBuffer.");
    }

    auto &req_msgbuf = c.req_msgbuf[msgbuf_index];
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

    c.req_vec.push_back(tag_t(get_rand_session_index(&c), msgbuf_index));
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

int main(int argc, char **argv) {
  assert(FLAGS_num_bg_threads == 0);  // XXX: Need to change ReqFuncType below
  signal(SIGINT, ctrl_c_handler);

  if (!ERpc::large_rpc_supported()) {
    throw std::runtime_error(
        "Current eRPC optlevel does not allow large RPCs.");
  }

  // Work around g++-5's unused variable warning for validators
  _unused(concurrency_validator_registered);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

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

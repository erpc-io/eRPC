/**
 * @file large_rpc_tput.cc
 *
 * @brief Benchmark to measure large RPC throughput. Each thread measures its
 * response RX and TX bandwidth. For this measurement to be useful, request
 * size should be small and response size large.
 *
 * Requests are described by a MsgBuffer index and a session index, and are
 * queued into req_vec until they can be transmitted. Before queueing a request
 * descriptor, the MsgBuffer for the request must be filled with request data.
 */

#include <gflags/gflags.h>
#include <papi.h>
#include <signal.h>
#include <cstring>
#include "rpc.h"
#include "util/misc.h"

static constexpr bool kAppVerbose = false;

// If true, we memset() request and respose buffers to kAppDataByte. If false,
// only the first data byte is touched.
static constexpr bool kAppMemset = true;

static constexpr size_t kAppNexusUdpPort = 31851;
static constexpr size_t kAppPhyPort = 0;
static constexpr size_t kAppNumaNode = 0;
static constexpr size_t kAppReqType = 1;
static constexpr uint8_t kAppDataByte = 3;   // Data transferred in req & resp
static constexpr size_t kAppTestMs = 50000;  // Test duration in milliseconds
static constexpr size_t kMaxConcurrency = 32;

DEFINE_uint64(num_machines, 0, "Number of machines in the cluster");
DEFINE_uint64(machine_id, ERpc::kMaxNumMachines, "The ID of this machine");
DEFINE_uint64(num_threads, 0, "Number of foreground threads per machine");
DEFINE_uint64(num_bg_threads, 0, "Number of background threads per machine");
DEFINE_uint64(req_size, 0, "Request data size");
DEFINE_uint64(resp_size, 0, "Response data size");
DEFINE_uint64(concurrency, 0, "Concurrent batches per thread");

static bool validate_machine_id(const char *, uint64_t machine_id) {
  return machine_id < ERpc::kMaxNumMachines;
}

static bool validate_concurrency(const char *, uint64_t concurrency) {
  return concurrency <= kMaxConcurrency;
}

DEFINE_validator(machine_id, &validate_machine_id);
DEFINE_validator(concurrency, &validate_concurrency);

volatile sig_atomic_t ctrl_c_pressed = 0;
void ctrl_c_handler(int) { ctrl_c_pressed = 1; }

// Return the control net IP address of the machine with index server_i
static std::string get_hostname_for_machine(size_t server_i) {
  std::ostringstream ret;
  // ret << "akaliaNode-" << std::to_string(server_i + 1)
  //    << ".RDMA.fawn.apt.emulab.net"
  ret << "3.1.8." << std::to_string(server_i + 1);
  return ret.str();
}

// Request tag, which doubles up as the request descriptor in req_vec
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

  ERpc::MsgBuffer req_msgbuf[kMaxConcurrency];  // MsgBuffers for requests

  // The response MsgBuffer created by eRPC is copied to this MsgBuffer. This
  // mimics an application copying the response to app-owned memory.
  ERpc::MsgBuffer resp_dest_msgbuf[kMaxConcurrency];
};

// A basic session management handler that expects successful responses
void basic_sm_handler(int, ERpc::SmEventType sm_event_type,
                      ERpc::SmErrType sm_err_type, void *_context) {
  _unused(sm_event_type);
  _unused(sm_err_type);

  auto *context = static_cast<AppContext *>(_context);
  context->num_sm_resps++;

  assert(sm_err_type == ERpc::SmErrType::kNoError);
  assert(sm_event_type == ERpc::SmEventType::kConnected ||
         sm_event_type == ERpc::SmEventType::kDisconnected);
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
      printf("Sending request for session %zu, msgbuf_index = %zu.\n",
             session_index, msgbuf_index);
    }

    int ret =
        c->rpc->enqueue_request(c->session_num_vec[session_index], kAppReqType,
                                &req_msgbuf, app_cont_func, c->req_vec[i]._tag);
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
    printf("Received response for msgbuf %zu, session %zu.\n", msgbuf_index,
           session_index);
  }

  // Check the response
  if (unlikely(resp_msgbuf->get_data_size() != FLAGS_resp_size)) {
    fprintf(stderr, "Invalid response size.\n");
    exit(-1);
  }

  if (unlikely(resp_msgbuf->buf[0] != kAppDataByte)) {
    fprintf(stderr, "Invalid response data.\n");
    exit(-1);
  }

  auto *c = static_cast<AppContext *>(_context);
  if (kAppMemset) {
    // Copy response to application's response destination
    memcpy(c->resp_dest_msgbuf[msgbuf_index].buf, resp_msgbuf->buf,
           FLAGS_resp_size);
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
    printf(
        "Thread %zu: Response tput: RX %.3f GB/s, TX %.3f GB/s. "
        "Response bytes: RX %.3f MB, TX = %.3f MB.\n",
        c->thread_id, c->stat_resp_rx_bytes_tot / ns,
        c->stat_resp_tx_bytes_tot / ns, c->stat_resp_rx_bytes_tot / 1000000.0,
        c->stat_resp_tx_bytes_tot / 1000000.0);

    printf("Tput per session: ");
    for (size_t i = 0; i < c->session_num_vec.size(); i++) {
      printf("%zu: %.2f, ", i, c->stat_resp_rx_bytes[i] / ns);
    }
    printf("\n");

    if (FLAGS_num_threads == 1) {
      float real_time, proc_time, ipc;
      long long ins;
      int ret = PAPI_ipc(&real_time, &proc_time, &ins, &ipc);
      if (ret < PAPI_OK) {
        fprintf(stderr, "PAPI IPC failed.\n");
        exit(-1);
      } else {
        printf("IPC = %.3f.\n", ipc);
      }
    }

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
  for (size_t m_i = 0; m_i < FLAGS_num_machines; m_i++) {
    const char *hostname = get_hostname_for_machine(m_i).c_str();

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

    if (ctrl_c_pressed == 1) {
      return;
    }
  }

  fprintf(stderr, "Thread %zu: All sessions connected. Running event loop.\n",
          thread_id);
  clock_gettime(CLOCK_REALTIME, &c.tput_t0);

  // Generate the initial requests
  for (size_t msgbuf_index = 0; msgbuf_index < FLAGS_concurrency;
       msgbuf_index++) {
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

    // Allocate response destination buffers
    c.resp_dest_msgbuf[msgbuf_index] = rpc.alloc_msg_buffer(FLAGS_resp_size);
    if (c.resp_dest_msgbuf[msgbuf_index].buf == nullptr) {
      throw std::runtime_error("Failed to pre-allocate resp MsgBuffer.");
    }
  }

  // Do PAPI measurement if we're running one thread
  if (FLAGS_num_threads == 1) {
    float real_time, proc_time, ipc;
    long long ins;
    int ret = PAPI_ipc(&real_time, &proc_time, &ins, &ipc);
    if (ret < PAPI_OK) {
      fprintf(stderr, "PAPI initialization failed.\n");
      exit(-1);
    }
  }

  // Send queued requests
  send_reqs(&c);

  for (size_t i = 0; i < kAppTestMs; i += 1000) {
    rpc.run_event_loop(1000);  // 1 second
    if (ctrl_c_pressed == 1) break;
  }
}

int main(int argc, char **argv) {
  assert(FLAGS_num_bg_threads == 0);  // XXX: Need to change ReqFuncType below
  signal(SIGINT, ctrl_c_handler);

  // g++-5 shows an unused variable warning for validators
  _unused(machine_id_validator_registered);
  _unused(concurrency_validator_registered);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string machine_name = get_hostname_for_machine(FLAGS_machine_id);
  ERpc::Nexus<ERpc::IBTransport> nexus(machine_name.c_str(), kAppNexusUdpPort,
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

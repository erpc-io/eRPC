#include <gflags/gflags.h>
#include <papi.h>
#include <signal.h>
#include <cstring>
#include "rpc.h"
#include "util/misc.h"

static constexpr bool kAppVerbose = false;

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
DEFINE_uint64(msg_size, 0, "Request and response size");
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

/// Return the control net IP address of the machine with index server_i
static std::string get_hostname_for_machine(size_t server_i) {
  std::ostringstream ret;
  // ret << "akaliaNode-" << std::to_string(server_i + 1)
  //    << ".RDMA.fawn.apt.emulab.net"
  ret << "3.1.8." << std::to_string(server_i + 1);
  return ret.str();
}

/// Per-thread application context
class AppContext {
 public:
  ERpc::Rpc<ERpc::IBTransport> *rpc = nullptr;

  /// Number of slots in session_arr, including an unused one for this thread
  size_t num_sessions;
  int *session_arr = nullptr;  ///< Sessions created as client

  /// The entry in session_arr for this thread, so we don't send reqs to ourself
  size_t self_session_index;
  size_t thread_id;         ///< The ID of the thread that owns this context
  size_t num_sm_resps = 0;  ///< Number of SM responses
  struct timespec tput_t0;  ///< Start time for throughput measurement
  ERpc::FastRand fastrand;

  size_t stat_resp_rx_tot = 0;  ///< Total responses received (all batches)
  size_t stat_req_rx_tot = 0;   ///< Total requests received (all batches)

  std::vector<size_t> req_vec;  ///< Request queue containing MsgBuffer indices

  ERpc::MsgBuffer req_msgbuf[kMaxConcurrency];  ///< MsgBuffers for requests
};

/// A basic session management handler that expects successful responses
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
  static_assert(sizeof(c->num_sessions) == 8, "");

  // Use Lemire's trick to compute random numbers modulo c->num_sessions
  uint32_t x = c->fastrand.next_u32();
  size_t rand_session_index = (static_cast<size_t>(x) * c->num_sessions) >> 32;
  while (rand_session_index == c->self_session_index) {
    x = c->fastrand.next_u32();
    rand_session_index = (static_cast<size_t>(x) * c->num_sessions) >> 32;
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
    size_t msgbuf_index = c->req_vec[i];

    ERpc::MsgBuffer &req_msgbuf = c->req_msgbuf[msgbuf_index];
    assert(req_msgbuf.get_data_size() == FLAGS_msg_size);

    size_t rand_session_index = get_rand_session_index(c);

    if (kAppVerbose) {
      printf("Sending request for session %zu, msgbuf_index = %zu.\n",
             rand_session_index, msgbuf_index);
    }

    // Use the MsgBuffer index as tag
    int ret =
        c->rpc->enqueue_request(c->session_arr[rand_session_index], kAppReqType,
                                &req_msgbuf, app_cont_func, msgbuf_index);
    assert(ret == 0 || ret == -EBUSY);

    if (ret == -EBUSY) {
      c->req_vec[write_index] = msgbuf_index;
      write_index++;
    }
  }

  c->req_vec.resize(write_index);  // Pending requests = write_index
}

void req_handler(ERpc::ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr);
  assert(_context != nullptr);

  auto *c = static_cast<AppContext *>(_context);
  c->stat_req_rx_tot++;

  const ERpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  size_t resp_size = req_msgbuf->get_data_size();

  // Use dynamic response
  req_handle->prealloc_used = false;
  ERpc::MsgBuffer &resp_msgbuf = req_handle->dyn_resp_msgbuf;
  resp_msgbuf = c->rpc->alloc_msg_buffer(resp_size);  // ERpc will free this
  assert(resp_msgbuf.buf != nullptr);

  memset(resp_msgbuf.buf, kAppDataByte, resp_size);  // Touch response bytes
  c->rpc->enqueue_response(req_handle);
}

void app_cont_func(ERpc::RespHandle *resp_handle, void *_context, size_t _tag) {
  assert(resp_handle != nullptr);
  assert(_context != nullptr);

  const ERpc::MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  assert(resp_msgbuf != nullptr);

  // Examine resp MsgBuffer
  if (unlikely(resp_msgbuf->get_data_size() != FLAGS_msg_size)) {
    fprintf(stderr, "Invalid response size.\n");
    exit(-1);
  }

  for (size_t i = 0; i < FLAGS_msg_size; i++) {
    if (unlikely(resp_msgbuf->buf[i] != kAppDataByte)) {
      fprintf(stderr, "Invalid response data.\n");
      exit(-1);
    }
  }

  size_t msgbuf_index = _tag;
  if (kAppVerbose) {
    printf("Received response for msgbuf %zu.\n", msgbuf_index);
  }

  auto *c = static_cast<AppContext *>(_context);

  // Create a new request clocking this response, and put in request queue
  ERpc::MsgBuffer &req_msgbuf = c->req_msgbuf[msgbuf_index];
  memset(req_msgbuf.buf, kAppDataByte, FLAGS_msg_size);
  c->req_vec.push_back(msgbuf_index);

  // Try to send the queued requests. The request buffer for these requests is
  // already filled.
  send_reqs(c);

  c->stat_resp_rx_tot++;

  if (c->stat_resp_rx_tot == 1000000) {
    double seconds = ERpc::sec_since(c->tput_t0);
    printf(
        "Thread %zu: Throughput = %.2f Mrps. Average TX batch size = %.2f. "
        "Responses received = %zu, requests received = %zu.\n",
        c->thread_id, c->stat_resp_rx_tot / seconds,
        c->rpc->get_avg_tx_burst_size(), c->stat_resp_rx_tot,
        c->stat_req_rx_tot);

    for (size_t i = 0; i < FLAGS_concurrency; i++) {
      printf("%zu, ", c->stat_resp_rx[i]);
      c->stat_resp_rx[i] = 0;
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
    c->stat_resp_rx_tot = 0;
    c->stat_req_rx_tot = 0;

    clock_gettime(CLOCK_REALTIME, &c->tput_t0);
  }

  c->rpc->release_response(resp_handle);
}

/// The function executed by each thread in the cluster
void thread_func(size_t thread_id, ERpc::Nexus<ERpc::IBTransport> *nexus) {
  AppContext c;
  c.thread_id = thread_id;
  c.num_sessions = FLAGS_num_machines * FLAGS_num_threads;
  c.self_session_index = FLAGS_machine_id * FLAGS_num_threads + thread_id;

  ERpc::Rpc<ERpc::IBTransport> rpc(nexus, static_cast<void *>(&c),
                                   static_cast<uint8_t>(thread_id),
                                   basic_sm_handler, kAppPhyPort, kAppNumaNode);
  rpc.retry_connect_on_invalid_rpc_id = true;
  c.rpc = &rpc;

  // Allocate session array
  c.session_arr = new int[FLAGS_num_machines * FLAGS_num_threads];
  for (size_t i = 0; i < FLAGS_num_machines * FLAGS_num_threads; i++) {
    c.session_arr[i] = -1;
  }

  // Initiate connection for sessions
  for (size_t m_i = 0; m_i < FLAGS_num_machines; m_i++) {
    const char *hostname = get_hostname_for_machine(m_i).c_str();

    for (size_t t_i = 0; t_i < FLAGS_num_threads; t_i++) {
      size_t session_index = (m_i * FLAGS_num_threads) + t_i;
      // Do not create a session to self
      if (session_index == c.self_session_index) continue;

      c.session_arr[session_index] =
          rpc.create_session(hostname, static_cast<uint8_t>(t_i), kAppPhyPort);
      assert(c.session_arr[session_index] >= 0);
    }
  }

  while (c.num_sm_resps != FLAGS_num_machines * FLAGS_num_threads - 1) {
    rpc.run_event_loop(200);  // 200 milliseconds

    if (ctrl_c_pressed == 1) {
      delete c.session_arr;
      return;
    }
  }

  fprintf(stderr, "Thread %zu: All sessions connected. Running event loop.\n",
          thread_id);
  clock_gettime(CLOCK_REALTIME, &c.tput_t0);

  // Pre-allocate request MsgBuffers
  for (size_t i = 0; i < FLAGS_concurrency; i++) {
    c.req_msgbuf[i] = rpc.alloc_msg_buffer(FLAGS_msg_size);
    assert(c.req_msgbuf[i].buf != nullptr);

    memset(c.req_msgbuf[i].buf, kAppDataByte, FLAGS_msg_size);  // Fill request
    c.req_vec.push_back(i);
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

  delete c.session_arr;
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
      kAppReqType, ERpc::ReqFunc(req_handler, ERpc::ReqFuncType::kFgTerminal));

  std::thread threads[FLAGS_num_threads];
  for (size_t i = 0; i < FLAGS_num_threads; i++) {
    threads[i] = std::thread(thread_func, i, &nexus);
    ERpc::bind_to_core(threads[i], i);
  }

  for (size_t i = 0; i < FLAGS_num_threads; i++) {
    threads[i].join();
  }
}

#include <gflags/gflags.h>
#include <signal.h>
#include <cstring>
#include "rpc.h"

static constexpr size_t kAppNexusUdpPort = 31851;
static constexpr size_t kAppPhyPort = 0;
static constexpr size_t kAppNumaNode = 0;
static constexpr size_t kAppReqType = 1;
static constexpr size_t kAppTestMs = 10000;  /// Test duration in milliseconds
static constexpr size_t kAppMaxBatchSize = 32;
static constexpr size_t kMaxConcurrency = 32;

DEFINE_uint64(num_machines, 0, "Number of machines in the cluster");
DEFINE_uint64(machine_id, ERpc::kMaxNumMachines, "The ID of this machine");
DEFINE_uint64(num_threads, 0, "Number of foreground threads per machine");
DEFINE_uint64(num_bg_threads, 0, "Number of background threads per machine");
DEFINE_uint64(msg_size, 0, "Request and response size");
DEFINE_uint64(batch_size, 0, "Request batch size");
DEFINE_uint64(concurrency, 0, "Concurrent batches per thread");

// XXX: g++-5 shows an unused variable warning for validators
// https://github.com/gflags/gflags/issues/123. Current fix: use g++-7
static bool validate_batch_size(const char *, uint64_t batch_size) {
  return batch_size <= kAppMaxBatchSize;
}

static bool validate_machine_id(const char *, uint64_t machine_id) {
  return machine_id < ERpc::kMaxNumMachines;
}

static bool validate_concurrency(const char *, uint64_t concurrency) {
  return concurrency <= kMaxConcurrency;
}

DEFINE_validator(machine_id, &validate_machine_id);
DEFINE_validator(batch_size, &validate_batch_size);
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

/// Per-batch context
class BatchContext {
 public:
  size_t num_reqs_sent = 0;   ///< Number of requests sent
  size_t num_resps_rcvd = 0;  ///< Number of responses received
  ERpc::MsgBuffer req_msgbuf[kAppMaxBatchSize];
};

/// Per-thread application context
class AppContext {
 public:
  ERpc::Rpc<ERpc::IBTransport> *rpc = nullptr;

  /// Number of slots in session_arr, including an unused one for this thread
  size_t num_sessions;
  int *session_arr = nullptr;  ///< Sessions created as client

  /// The entry in session_arr for this thread, so we don't send reqs to ourself
  size_t self_session_index;
  size_t thread_id;           ///< The ID of the thread that owns this context
  size_t num_sm_resps = 0;    ///< Number of SM responses
  size_t stat_rpc_resps = 0;  ///< Number of responses received
  struct timespec tput_t0;    ///< Start time for throughput measurement
  ERpc::FastRand fastrand;

  std::array<BatchContext, kMaxConcurrency> batch_arr;  ///< Per-batch context
  ERpc::FixedVector<size_t, kMaxConcurrency> batch_queue;
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

  size_t rand_session_index = c->self_session_index;
  while (rand_session_index == c->self_session_index) {
    rand_session_index = c->fastrand.next_u32() % c->num_sessions;
  }

  return rand_session_index;
}

void cont_func(ERpc::RespHandle *, void *, size_t);  // Forward declaration
void send_reqs(AppContext *c, size_t batch_i) {
  assert(c != nullptr);
  assert(batch_i < FLAGS_batch_size);

  BatchContext &bc = c->batch_arr[batch_i];

  for (size_t i = 0; i < FLAGS_batch_size - bc.num_reqs_sent; i++) {
    // Compute a random session to send the request on
    size_t rand_session_index = get_rand_session_index(c);
    size_t msgbuf_index = bc.num_reqs_sent + i;

    int ret = c->rpc->enqueue_request(c->session_arr[rand_session_index],
                                      kAppReqType, &bc.req_msgbuf[msgbuf_index],
                                      cont_func, batch_i);
    assert(ret == 0 || ret == -EBUSY);

    if (ret == -EBUSY) {
      if (bc.num_reqs_sent == bc.num_resps_rcvd) {
        c->batch_queue.push_back(batch_i);
      }
    } else {
      bc.num_reqs_sent++;
    }
  }
}

void req_handler(ERpc::ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr);
  assert(_context != nullptr);

  auto *context = static_cast<AppContext *>(_context);

  const ERpc::MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  size_t resp_size = req_msgbuf->get_data_size();
  ERpc::Rpc<ERpc::IBTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                                  resp_size);
  memcpy(static_cast<void *>(req_handle->pre_resp_msgbuf.buf),
         static_cast<void *>(req_msgbuf->buf), resp_size);
  req_handle->prealloc_used = true;

  context->rpc->enqueue_response(req_handle);
}

void cont_func(ERpc::RespHandle *resp_handle, void *_context, size_t batch_i) {
  assert(resp_handle != nullptr);
  assert(_context != nullptr);

  const ERpc::MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  assert(resp_msgbuf != nullptr);
  _unused(resp_msgbuf);

  // XXX: This can be removed
  // for (size_t i = 0; i < FLAGS_msg_size; i++) {
  //  assert(resp_msgbuf->buf[i] == static_cast<uint8_t>(tag));
  //}

  auto *c = static_cast<AppContext *>(_context);
  BatchContext &bc = c->batch_arr[batch_i];
  bc.num_resps_rcvd++;

  if (bc.num_resps_rcvd == bc.num_reqs_sent) {
    bc.num_reqs_sent = 0;
    bc.num_resps_rcvd = 0;
    send_reqs(c, batch_i);
  }

  if (c->batch_queue.size() != 0) {
    for (size_t i = 0; i < c->batch_queue.size(); i++) {
      size_t queued_batch_i = c->batch_queue[i];
      assert(queued_batch_i != batch_i);

      send_reqs(c, queued_batch_i);
    }
  }

  c->stat_rpc_resps++;

  if (c->stat_rpc_resps == 1000000) {
    double seconds = ERpc::sec_since(c->tput_t0);
    printf("Thread %zu: Throughput = %.2f Mrps.\n", c->thread_id,
           1000000 / seconds);

    c->stat_rpc_resps = 0;
    clock_gettime(CLOCK_REALTIME, &c->tput_t0);
  }

  c->rpc->release_response(resp_handle);
}

/// The function executed by each thread in the cluster
void thread_func(size_t thread_id, ERpc::Nexus<ERpc::IBTransport> *nexus) {
  AppContext context;
  context.thread_id = thread_id;

  ERpc::Rpc<ERpc::IBTransport> rpc(nexus, static_cast<void *>(&context),
                                   static_cast<uint8_t>(thread_id),
                                   basic_sm_handler, kAppPhyPort, kAppNumaNode);
  rpc.retry_connect_on_invalid_rpc_id = true;
  context.rpc = &rpc;

  // Pre-allocate request MsgBuffers for each batch
  for (size_t i = 0; i < FLAGS_concurrency; i++) {
    for (size_t j = 0; j < FLAGS_batch_size; j++) {
      context.batch_arr[i].req_msgbuf[j] = rpc.alloc_msg_buffer(FLAGS_msg_size);
      assert(context.batch_arr[i].req_msgbuf[j].buf != nullptr);
    }
  }

  context.num_sessions = FLAGS_num_machines * FLAGS_num_threads;
  context.self_session_index = FLAGS_machine_id * FLAGS_num_threads + thread_id;

  // Allocate session array
  context.session_arr = new int[FLAGS_num_machines * FLAGS_num_threads];
  for (size_t i = 0; i < FLAGS_num_machines * FLAGS_num_threads; i++) {
    context.session_arr[i] = -1;
  }

  // Initiate connection for sessions
  for (size_t m_i = 0; m_i < FLAGS_num_machines; m_i++) {
    const char *hostname = get_hostname_for_machine(m_i).c_str();

    for (size_t t_i = 0; t_i < FLAGS_num_threads; t_i++) {
      size_t session_index = (m_i * FLAGS_num_threads) + t_i;
      // Do not create a session to self
      if (session_index == context.self_session_index) continue;

      context.session_arr[session_index] =
          rpc.create_session(hostname, static_cast<uint8_t>(t_i), kAppPhyPort);
      assert(context.session_arr[session_index] >= 0);
    }
  }

  while (context.num_sm_resps != FLAGS_num_machines * FLAGS_num_threads - 1) {
    rpc.run_event_loop_timeout(200);  // 200 milliseconds

    if (ctrl_c_pressed == 1) {
      delete context.session_arr;
      return;
    }
  }

  fprintf(stderr, "Thread %zu: All sessions connected. Running event loop.\n",
          thread_id);
  clock_gettime(CLOCK_REALTIME, &context.tput_t0);

  for (size_t i = 0; i < FLAGS_batch_size; i++) {
    send_reqs(&context, i);
  }

  for (size_t i = 0; i < kAppTestMs; i += 1000) {
    rpc.run_event_loop_timeout(1000);  // 1 second
    if (ctrl_c_pressed == 1) break;
  }

  delete context.session_arr;
}

int main(int argc, char **argv) {
  assert(FLAGS_num_bg_threads == 0);  // XXX: Need to change ReqFuncType below
  signal(SIGINT, ctrl_c_handler);

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string machine_name = get_hostname_for_machine(FLAGS_machine_id);
  ERpc::Nexus<ERpc::IBTransport> nexus(machine_name.c_str(), kAppNexusUdpPort,
                                       FLAGS_num_bg_threads);
  nexus.register_req_func(
      kAppReqType, ERpc::ReqFunc(req_handler, ERpc::ReqFuncType::kFgTerminal));

  std::thread *threads[FLAGS_num_threads];
  for (size_t i = 0; i < FLAGS_num_threads; i++) {
    threads[i] = new std::thread(thread_func, i, &nexus);
  }

  for (size_t i = 0; i < FLAGS_num_threads; i++) {
    threads[i]->join();
  }
}

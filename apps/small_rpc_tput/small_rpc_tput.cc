#include <gflags/gflags.h>
#include <cstring>
#include "rpc.h"

static constexpr size_t kAppNexusUdpPort = 31851;
static constexpr size_t kAppPhyPort = 0;
static constexpr size_t kAppNumaNode = 0;
static constexpr size_t kAppReqType = 1;
static constexpr size_t kAppTestMs = 10000;  /// Test duration in milliseconds
static constexpr size_t kAppMaxBatchSize = 32;

DEFINE_uint64(num_machines, 0, "Number of machines in the cluster");
DEFINE_uint64(machine_id, 0, "The ID of this machine");
DEFINE_uint64(num_threads, 0, "Number of foreground threads per machine");
DEFINE_uint64(num_bg_threads, 0, "Number of background threads per machine");
DEFINE_uint64(msg_size, 0, "Request and response size");
DEFINE_uint64(batch_size, 0, "Request batch size");
DEFINE_uint64(window_size, 0, "Number of outstanding requests per thread");

static bool validate_batch_size(const char *, uint64_t batch_size) {
  return batch_size <= kAppMaxBatchSize;
}

DEFINE_validator(batch_size, &validate_batch_size);

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

  ERpc::MsgBuffer req_msgbuf[kAppMaxBatchSize];

  /// The entry in session_arr for this thread, so we don't send reqs to ourself
  size_t my_session_offset;
  size_t thread_id;          ///< The ID of the thread that owns this context
  size_t num_sm_resps = 0;   ///< Number of SM responses
  size_t num_rpc_resps = 0;  ///< Number of Rpc responses
  ERpc::FastRand fastrand;
};

/// A basic session management handler that expects successful responses
void basic_sm_handler(int session_num, ERpc::SmEventType sm_event_type,
                      ERpc::SmErrType sm_err_type, void *_context) {
  _unused(session_num);
  _unused(sm_event_type);
  _unused(sm_err_type);
  _unused(_context);

  auto *context = static_cast<AppContext *>(_context);
  context->num_sm_resps++;

  assert(sm_err_type == ERpc::SmErrType::kNoError);
  assert(sm_event_type == ERpc::SmEventType::kConnected ||
         sm_event_type == ERpc::SmEventType::kDisconnected);
}

void cont_func(ERpc::RespHandle *, void *, size_t);  // Forward declaration
void send_req_batch(AppContext *c) {
  assert(c != nullptr);
  for (size_t i = 0; i < FLAGS_msg_size; i++) {
    size_t rand_session_offset = c->my_session_offset;
    while (rand_session_offset == c->my_session_offset) {
      rand_session_offset = c->fastrand.next_u32() % c->num_sessions;
    }

    int ret =
        c->rpc->enqueue_request(c->session_arr[rand_session_offset],
                                kAppReqType, &c->req_msgbuf[i], cont_func, 0);
    if (ret != 0) throw std::runtime_error("Enqueue request error.");
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

void cont_func(ERpc::RespHandle *resp_handle, void *_context, size_t tag) {
  assert(resp_handle != nullptr);
  assert(_context != nullptr);
  _unused(tag);

  const ERpc::MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  assert(resp_msgbuf != nullptr);

  printf("Received response.\n");

  // XXX: This can be removed
  // for (size_t i = 0; i < FLAGS_msg_size; i++) {
  //  assert(resp_msgbuf->buf[i] == static_cast<uint8_t>(tag));
  //}

  auto *context = static_cast<AppContext *>(_context);
  context->num_rpc_resps++;
  context->rpc->release_response(resp_handle);

  if (context->num_rpc_resps == FLAGS_batch_size) {
    context->num_rpc_resps = 0;
    send_req_batch(context);
  }
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

  // Pre-allocate request MsgBuffers
  for (size_t i = 0; i < FLAGS_batch_size; i++) {
    context.req_msgbuf[i] = rpc.alloc_msg_buffer(FLAGS_msg_size);
    assert(context.req_msgbuf[i].buf != nullptr);
  }

  // Allocate session array
  context.num_sessions = FLAGS_num_machines * FLAGS_num_threads;
  context.session_arr = new int[FLAGS_num_machines * FLAGS_num_threads];
  for (size_t i = 0; i < FLAGS_num_machines * FLAGS_num_threads; i++) {
    context.session_arr[i] = -1;
  }

  // Initiate session connection
  size_t session_index = 0;
  for (size_t machine_i = 0; machine_i < FLAGS_num_machines; machine_i++) {
    const char *hostname = get_hostname_for_machine(machine_i).c_str();

    for (size_t thread_i = 0; thread_i < FLAGS_num_threads; thread_i++) {
      // Do not create a session to self
      if (machine_i == FLAGS_machine_id && thread_i == thread_id) continue;

      context.session_arr[session_index] = rpc.create_session(
          hostname, static_cast<uint8_t>(thread_id), kAppPhyPort);
      session_index++;
    }
  }

  while (context.num_sm_resps != FLAGS_num_machines * FLAGS_num_threads - 1) {
    rpc.run_event_loop_timeout(200);  // 200 milliseconds
  }

  // All sessions connected, so run event loop
  send_req_batch(&context);
  rpc.run_event_loop_timeout(kAppTestMs);
  delete context.session_arr;
}

int main(int argc, char **argv) {
  assert(FLAGS_num_bg_threads == 0);  // XXX: Need to change ReqFuncType below

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

#include <gflags/gflags.h>
#include "rpc.h"

using namespace ERpc;

DEFINE_uint64(num_machines, 0, "Number of machines in the cluster");
DEFINE_uint64(machine_id, 0, "The ID of this machine");
DEFINE_uint64(num_threads, 0, "Number of threads per machine");
DEFINE_uint64(num_bg_threads, 0, "Number of background threads per machine");
DEFINE_uint64(msg_size, 0, "Request and response size");
DEFINE_uint64(window_size, 0, "Number of outstanding requests per thread");

static constexpr size_t kAppNexusUdpPort = 31851;
static constexpr size_t kAppPhyPort = 0;
static constexpr size_t kAppNumaNode = 0;
static constexpr size_t kAppReqType = 1;
static constexpr size_t kAppTestMs = 10000;  /// Test duration in milliseconds

/// Per-thread application context
class AppContext {
 public:
  Rpc<IBTransport> *rpc = nullptr;
  int *session_num_arr = nullptr;  ///< Sessions created as client

  size_t num_sm_resps = 0;   ///< Number of SM responses
  size_t num_rpc_resps = 0;  ///< Number of Rpc responses
};

/// A basic session management handler that expects successful responses
void basic_sm_handler(int session_num, SmEventType sm_event_type,
                      SmErrType sm_err_type, void *_context) {
  _unused(session_num);
  _unused(sm_event_type);
  _unused(sm_err_type);
  _unused(_context);

  auto *context = static_cast<AppContext *>(_context);
  context->num_sm_resps++;

  assert(sm_err_type == SmErrType::kNoError);
  assert(sm_event_type == SmEventType::kConnected ||
         sm_event_type == SmEventType::kDisconnected);
}

void req_handler(ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr);
  assert(_context != nullptr);

  auto *context = static_cast<AppContext *>(_context);

  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  size_t resp_size = req_msgbuf->get_data_size();
  Rpc<IBTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf, resp_size);
  memcpy(static_cast<void *>(req_handle->pre_resp_msgbuf.buf),
         static_cast<void *>(req_msgbuf->buf), resp_size);
  req_handle->prealloc_used = true;

  context->rpc->enqueue_response(req_handle);
}

void cont_func(RespHandle *resp_handle, void *_context, size_t tag) {
  assert(resp_handle != nullptr);
  assert(_context != nullptr);

  const MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  assert(resp_msgbuf != nullptr);

  // XXX: This can be removed
  for (size_t i = 0; i < FLAGS_msg_size; i++) {
    assert(resp_msgbuf->buf[i] == static_cast<uint8_t>(tag));
  }

  auto *context = static_cast<AppContext *>(_context);
  context->num_rpc_resps++;

  context->rpc->release_response(resp_handle);
}

void generic_test_func(Nexus<IBTransport> *nexus, size_t) {
  // Create the Rpc and connect the session
  AppContext context;
  client_connect_sessions(nexus, context, config_num_sessions,
                          basic_sm_handler);

  Rpc<IBTransport> *rpc = context.rpc;
  int *session_num_arr = context.session_num_arr;

  // Pre-create MsgBuffers so we can test reuse and resizing
  size_t tot_reqs_per_iter = config_num_sessions * config_rpcs_per_session;
  MsgBuffer req_msgbuf[tot_reqs_per_iter];
  for (size_t req_i = 0; req_i < tot_reqs_per_iter; req_i++) {
    req_msgbuf[req_i] = rpc->alloc_msg_buffer(rpc->get_max_data_per_pkt());
    assert(req_msgbuf[req_i].buf != nullptr);
  }

  // The main request-issuing loop
  for (size_t iter = 0; iter < 2; iter++) {
    context.num_rpc_resps = 0;

    test_printf("Client: Iteration %zu.\n", iter);
    size_t iter_req_i = 0;  // Request MsgBuffer index in an iteration

    for (size_t sess_i = 0; sess_i < config_num_sessions; sess_i++) {
      for (size_t w_i = 0; w_i < config_rpcs_per_session; w_i++) {
        assert(iter_req_i < tot_reqs_per_iter);
        MsgBuffer &cur_req_msgbuf = req_msgbuf[iter_req_i];

        rpc->resize_msg_buffer(&cur_req_msgbuf, config_msg_size);
        for (size_t i = 0; i < config_msg_size; i++) {
          cur_req_msgbuf.buf[i] = static_cast<uint8_t>(iter_req_i);
        }

        int ret = rpc->enqueue_request(session_num_arr[sess_i], kAppReqType,
                                       &cur_req_msgbuf, cont_func, iter_req_i);
        if (ret != 0) {
          test_printf("Client: enqueue_request error %s\n", std::strerror(ret));
        }
        assert(ret == 0);

        iter_req_i++;
      }
    }

    wait_for_rpc_resps_or_timeout(context, tot_reqs_per_iter, nexus->freq_ghz);
    assert(context.num_rpc_resps == tot_reqs_per_iter);
  }

  // Free the request MsgBuffers
  for (size_t req_i = 0; req_i < tot_reqs_per_iter; req_i++) {
    rpc->free_msg_buffer(req_msgbuf[req_i]);
  }

  // Disconnect the sessions
  for (size_t sess_i = 0; sess_i < config_num_sessions; sess_i++) {
    rpc->destroy_session(session_num_arr[sess_i]);
  }

  rpc->run_event_loop_timeout(kAppEventLoopMs);

  // Free resources
  delete rpc;
  client_done = true;
}

/// The function executed by each thread in the cluster
void thread_func(size_t thread_id, Nexus<IBTransport> *nexus) {
  uint8_t rpc_id = static_cast<uint8_t>(thread_id);
  AppContext context;

  Rpc<IBTransport> rpc(nexus, static_cast<void *>(&context), rpc_id,
                       basic_sm_handler, kAppPhyPort, kAppNumaNode);
  rpc.retry_connect_on_invalid_rpc_id = true;
  context.rpc = &rpc;

  // Create a session to each thread in the cluster
  size_t num_sessions = FLAGS_num_machines * FLAGS_num_threads;

  rpc.run_event_loop();
}

int main(int argc, char **argv) {
  assert(FLAGS_num_bg_threads == 0);  // XXX: Need to change ReqFuncType below

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  Nexus<IBTransport> nexus(kAppNexusUdpPort, FLAGS_num_bg_threads);
  nexus.register_req_func(kAppReqType,
                          ReqFunc(req_handler, ReqFuncType::kFgTerminal));

  std::thread *threads[FLAGS_num_threads];
  for (size_t i = 0; i < FLAGS_num_threads; i++) {
    threads[i] = new std::thread(thread_func, i, &nexus);
  }

  for (size_t i = 0; i < FLAGS_num_threads; i++) {
    threads[i]->join();
  }
}

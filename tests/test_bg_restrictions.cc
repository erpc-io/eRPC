#include <gtest/gtest.h>
#include <string.h>
#include <atomic>
#include <cstring>
#include <thread>
#include "rpc.h"
#include "util/test_printf.h"

using namespace ERpc;

static constexpr uint16_t kAppNexusUdpPort = 31851;
static constexpr double kAppNexusPktDropProb = 0.0;
static constexpr size_t kAppEventLoopMs = 200;
static constexpr size_t kAppMaxEventLoopMs = 20000; /* 20 seconds */
static constexpr uint8_t kAppClientAppTid = 100;
static constexpr uint8_t kAppServerAppTid = 200;
static constexpr uint8_t kAppReqType = 3;
static constexpr size_t kAppMaxMsgSize = 64;

/* Shared between client and server thread */
std::atomic<bool> server_ready; /* Client starts after server is ready */
std::atomic<bool> client_done;  /* Server ends after client is done */

const uint8_t phy_port = 0;
const size_t numa_node = 0;
char local_hostname[kMaxHostnameLen];

/// Per-thread application context
class AppContext {
 public:
  bool is_client;
  Rpc<IBTransport> *rpc;
  int *session_num_arr;

  size_t num_sm_connect_resps = 0; /* Client-only */
  size_t num_rpc_resps = 0;        /* Client-only */
};

/// The common request handler for all subtests. Copies the request string to
/// the response.
void req_handler(ReqHandle *req_handle, const MsgBuffer *req_msgbuf,
                 void *_context) {
  assert(req_handle != nullptr);
  assert(req_msgbuf != nullptr);
  assert(_context != nullptr);

  auto *context = (AppContext *)_context;
  ASSERT_FALSE(context->is_client);
  ASSERT_TRUE(context->rpc->in_background());

  test_printf("Server: Received request %s\n", req_msgbuf->buf);

  /* Try to create a session */
  int session_num =
      context->rpc->create_session(local_hostname, kAppServerAppTid, phy_port);
  ASSERT_EQ(session_num, -EPERM);

  /* Try to destroy a valid session number */
  int ret = context->rpc->destroy_session(0);
  ASSERT_EQ(ret, -EPERM);

  /*
   * Try to run the event loop. This may not cause a crash if asserts are
   * disabled.
   */
  ASSERT_DEATH(context->rpc->run_event_loop_one(), ".*");
  ASSERT_DEATH(assert(false), ".*");

  size_t resp_size = strlen((char *)req_msgbuf->buf);
  Rpc<IBTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf, resp_size);
  strcpy((char *)req_handle->pre_resp_msgbuf.buf, (char *)req_msgbuf->buf);
  req_handle->prealloc_used = true;

  context->rpc->enqueue_response(req_handle);
}

/// The common continuation function for all subtests. This checks that the
/// request buffer is identical to the response buffer, and increments the
/// number of responses in the context.
void cont_func(RespHandle *resp_handle, const MsgBuffer *resp_msgbuf,
               void *_context, size_t tag) {
  assert(resp_handle != nullptr);
  assert(resp_msgbuf != nullptr);
  assert(_context != nullptr);

  test_printf("Client: Received response %s, tag = %zu\n",
              (char *)resp_msgbuf->buf, tag);

  std::string exp_resp = std::string("APP_MSG-") + std::to_string(tag);
  ASSERT_STREQ((char *)resp_msgbuf->buf, exp_resp.c_str());

  auto *context = (AppContext *)_context;
  ASSERT_TRUE(context->is_client);
  context->num_rpc_resps++;

  context->rpc->release_respone(resp_handle);
}

/// The common session management handler for all subtests
void sm_hander(int session_num, SessionMgmtEventType sm_event_type,
               SessionMgmtErrType sm_err_type, void *_context) {
  _unused(session_num);

  auto *context = (AppContext *)_context;
  ASSERT_TRUE(context->is_client);
  context->num_sm_connect_resps++;

  ASSERT_EQ(sm_err_type, SessionMgmtErrType::kNoError);
  ASSERT_TRUE(sm_event_type == SessionMgmtEventType::kConnected ||
              sm_event_type == SessionMgmtEventType::kDisconnected);
}

/// The server thread used for all subtests
void server_thread_func(Nexus *nexus, uint8_t app_tid) {
  AppContext context;
  context.is_client = false;

  Rpc<IBTransport> rpc(nexus, (void *)&context, app_tid, &sm_hander, phy_port,
                       numa_node);
  context.rpc = &rpc;
  server_ready = true;

  while (!client_done) { /* Wait for the client */
    rpc.run_event_loop_timeout(kAppEventLoopMs);
  }

  /* The client is done after disconnecting */
  ASSERT_EQ(rpc.num_active_sessions(), 0);
}

/**
 * @brief Launch (possibly) multiple server threads and one client thread
 *
 * @param num_sessions The number of sessions needed by the client thread,
 * equal to the number of server threads launched
 *
 * @param num_bg_threads The number of background threads in the Nexus. If
 * this is non-zero, the request handler is executed in a background thread.
 *
 * @param client_thread_func The function executed by the client threads
 */
void launch_server_client_threads(size_t num_sessions, size_t num_bg_threads,
                                  void (*client_thread_func)(Nexus *, size_t)) {
  Nexus nexus(kAppNexusUdpPort, num_bg_threads, kAppNexusPktDropProb);

  if (num_bg_threads == 0) {
    nexus.register_req_func(
        kAppReqType, ReqFunc(req_handler, ReqFuncType::kForegroundTerminal));
  } else {
    nexus.register_req_func(kAppReqType,
                            ReqFunc(req_handler, ReqFuncType::kBackground));
  }

  server_ready = false;
  client_done = false;

  test_printf("test: Using %zu sessions\n", num_sessions);

  std::thread server_thread[num_sessions];

  /* Launch one server Rpc thread for each client session */
  for (size_t i = 0; i < num_sessions; i++) {
    server_thread[i] =
        std::thread(server_thread_func, &nexus, kAppServerAppTid + i);
  }

  std::thread client_thread(client_thread_func, &nexus, num_sessions);

  for (size_t i = 0; i < num_sessions; i++) {
    server_thread[i].join();
  }

  client_thread.join();
}

/// Initialize client context and connect sessions
void client_connect_sessions(Nexus *nexus, AppContext &context,
                             size_t num_sessions) {
  assert(nexus != nullptr);
  assert(num_sessions >= 1);

  while (!server_ready) { /* Wait for server */
    usleep(1);
  }

  context.is_client = true;
  context.rpc = new Rpc<IBTransport>(nexus, (void *)&context, kAppClientAppTid,
                                     &sm_hander, phy_port, numa_node);

  /* Connect the sessions */
  context.session_num_arr = new int[num_sessions];
  for (size_t i = 0; i < num_sessions; i++) {
    context.session_num_arr[i] = context.rpc->create_session(
        local_hostname, kAppServerAppTid + (uint8_t)i, phy_port);
  }

  while (context.num_sm_connect_resps < num_sessions) {
    context.rpc->run_event_loop_one();
  }

  /* sm handler checks that the callbacks have no errors */
  ASSERT_EQ(context.num_sm_connect_resps, num_sessions);
}

/// Run the event loop until we get \p num_resps RPC responses, or until
/// kAppMaxEventLoopMs are elapsed.
void client_wait_for_rpc_resps_or_timeout(const Nexus *nexus,
                                          AppContext &context,
                                          size_t num_resps) {
  /* Run the event loop for up to kAppMaxEventLoopMs milliseconds */
  uint64_t cycles_start = rdtsc();
  while (context.num_rpc_resps != num_resps) {
    context.rpc->run_event_loop_timeout(kAppEventLoopMs);

    double ms_elapsed = to_msec(rdtsc() - cycles_start, nexus->freq_ghz);
    if (ms_elapsed > kAppMaxEventLoopMs) {
      break;
    }
  }
}

///
/// Test: Send one small request packet and check that we receive the
/// correct response
///
void one_small_rpc(Nexus *nexus, size_t num_sessions = 1) {
  /* Create the Rpc and connect the session */
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions);

  Rpc<IBTransport> *rpc = context.rpc;
  int session_num = context.session_num_arr[0];

  /* Send a message */
  MsgBuffer req_msgbuf = rpc->alloc_msg_buffer(strlen("APP_MSG-0") + 1);
  ASSERT_NE(req_msgbuf.buf, nullptr);
  strcpy((char *)req_msgbuf.buf, "APP_MSG-0");

  test_printf("test: Sending request %s\n", (char *)req_msgbuf.buf);
  int ret =
      rpc->enqueue_request(session_num, kAppReqType, &req_msgbuf, cont_func, 0);
  if (ret != 0) {
    test_printf("test: enqueue_request error %s\n", std::strerror(ret));
  }
  ASSERT_EQ(ret, 0);

  client_wait_for_rpc_resps_or_timeout(nexus, context, 1);
  ASSERT_EQ(context.num_rpc_resps, 1);

  rpc->free_msg_buffer(req_msgbuf);

  /* Disconnect the session */
  rpc->destroy_session(session_num);
  rpc->run_event_loop_timeout(kAppEventLoopMs);

  /* Free resources */
  delete rpc;

  client_done = true;
}

TEST(BgRestrictions, All) {
  /* One background thread */
  launch_server_client_threads(1, 1, one_small_rpc);
}

int main(int argc, char **argv) {
  Nexus::get_hostname(local_hostname);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

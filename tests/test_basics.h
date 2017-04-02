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
static constexpr uint8_t kAppPhyPort = 0;
static constexpr size_t kAppNumaNode = 0;

/* Shared between client and server thread */
std::atomic<bool> server_ready; /* Clients starts after server is ready */
std::atomic<bool> client_done;  /* Server ends after clients are done */
char local_hostname[kMaxHostnameLen];

/*
 * Hack: The server threads check that their Rpcs have zero active sessions
 * after the client exits. This needs to be disabled for test_create_session,
 * which does not use disconnection.
 */
bool server_check_all_disconnected = true;

/// Basic context to derive from
class BasicAppContext {
 public:
  bool is_client;
  Rpc<IBTransport> *rpc;
  int *session_num_arr;

  size_t num_sm_resps = 0;   ///< Number of SM responses
  size_t num_rpc_resps = 0;  ///< Number of Rpc responses
};

/// A basic session management handler that expects successful responses
void basic_sm_handler(int session_num, SessionMgmtEventType sm_event_type,
                      SessionMgmtErrType sm_err_type, void *_context) {
  _unused(session_num);

  auto *context = (BasicAppContext *)_context;
  ASSERT_TRUE(context->is_client);
  context->num_sm_resps++;

  ASSERT_EQ(sm_err_type, SessionMgmtErrType::kNoError);
  ASSERT_TRUE(sm_event_type == SessionMgmtEventType::kConnected ||
              sm_event_type == SessionMgmtEventType::kDisconnected);
}

/// A basic empty session management handler that should never be iinvoked.
void basic_empty_sm_handler(int, SessionMgmtEventType, SessionMgmtErrType,
                            void *) {
  assert(false);
  exit(-1);
}

/// A basic request handler that should never be invoked
void basic_empty_req_handler(ReqHandle *, void *) {
  assert(false);
  exit(-1);
}

/// A basic server thread that just runs the event loop, and expects the
/// client to disconnect before finishing.
void basic_server_thread_func(Nexus<IBTransport> *nexus, uint8_t app_tid,
                              session_mgmt_handler_t sm_handler) {
  BasicAppContext context;
  context.is_client = false;

  Rpc<IBTransport> rpc(nexus, (void *)&context, app_tid, sm_handler,
                       kAppPhyPort, kAppNumaNode);
  context.rpc = &rpc;
  server_ready = true;

  while (!client_done) { /* Wait for all clients */
    rpc.run_event_loop_timeout(kAppEventLoopMs);
  }

  if (server_check_all_disconnected) {
    /* The client is done only after disconnecting */
    ASSERT_EQ(rpc.num_active_sessions(), 0);
  }
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
 * @param client_thread_func The function executed by the client threads.
 * Server threads execute \p basic_server_thread_func()
 *
 * @param req_func The request function that handlers kAppReqType
 */
void launch_server_client_threads(
    size_t num_sessions, size_t num_bg_threads,
    void (*client_thread_func)(Nexus<IBTransport> *, size_t),
    erpc_req_func_t req_func) {
  Nexus<IBTransport> nexus(kAppNexusUdpPort, num_bg_threads,
                           kAppNexusPktDropProb);

  if (num_bg_threads == 0) {
    nexus.register_req_func(kAppReqType,
                            ReqFunc(req_func, ReqFuncType::kFgTerminal));
  } else {
    nexus.register_req_func(kAppReqType,
                            ReqFunc(req_func, ReqFuncType::kBackground));
  }

  server_ready = false;
  client_done = false;

  test_printf("test: Using %zu sessions\n", num_sessions);

  std::thread server_thread[num_sessions];

  /* Launch one server Rpc thread for each client session */
  for (size_t i = 0; i < num_sessions; i++) {
    server_thread[i] =
        std::thread(basic_server_thread_func, &nexus, kAppServerAppTid + i,
                    basic_empty_sm_handler);
  }

  std::thread client_thread(client_thread_func, &nexus, num_sessions);

  for (size_t i = 0; i < num_sessions; i++) {
    server_thread[i].join();
  }

  client_thread.join();
}

/// Initialize client context and connect sessions
void client_connect_sessions(Nexus<IBTransport> *nexus,
                             BasicAppContext &context, size_t num_sessions,
                             session_mgmt_handler_t sm_handler) {
  assert(nexus != nullptr);
  assert(num_sessions >= 1);

  while (!server_ready) { /* Wait for server */
    usleep(1);
  }

  context.is_client = true;
  context.rpc = new Rpc<IBTransport>(nexus, (void *)&context, kAppClientAppTid,
                                     sm_handler, kAppPhyPort, kAppNumaNode);

  /* Connect the sessions */
  context.session_num_arr = new int[num_sessions];
  for (size_t i = 0; i < num_sessions; i++) {
    context.session_num_arr[i] = context.rpc->create_session(
        local_hostname, kAppServerAppTid + (uint8_t)i, kAppPhyPort);
  }

  while (context.num_sm_resps < num_sessions) {
    context.rpc->run_event_loop_one();
  }

  /* basic_sm_handler checks that the callbacks have no errors */
  ASSERT_EQ(context.num_sm_resps, num_sessions);
}

/// Run the event loop until we get at least \p num_resps session management
/// responses, or until kAppMaxEventLoopMs are elapsed.
void client_wait_for_sm_resps_or_timeout(const Nexus<IBTransport> *nexus,
                                         BasicAppContext &context,
                                         size_t num_resps) {
  /* Run the event loop for up to kAppMaxEventLoopMs milliseconds */
  uint64_t cycles_start = rdtsc();
  while (context.num_sm_resps < num_resps) {
    context.rpc->run_event_loop_timeout(kAppEventLoopMs);

    double ms_elapsed = to_msec(rdtsc() - cycles_start, nexus->freq_ghz);
    if (ms_elapsed > kAppMaxEventLoopMs) {
      break;
    }
  }
}

/// Run the event loop until we get at least \p num_resps RPC responses, or
/// until kAppMaxEventLoopMs are elapsed.
void client_wait_for_rpc_resps_or_timeout(const Nexus<IBTransport> *nexus,
                                          BasicAppContext &context,
                                          size_t num_resps) {
  /* Run the event loop for up to kAppMaxEventLoopMs milliseconds */
  uint64_t cycles_start = rdtsc();
  while (context.num_rpc_resps < num_resps) {
    context.rpc->run_event_loop_timeout(kAppEventLoopMs);

    double ms_elapsed = to_msec(rdtsc() - cycles_start, nexus->freq_ghz);
    if (ms_elapsed > kAppMaxEventLoopMs) {
      break;
    }
  }
}

#include <gtest/gtest.h>
#include <atomic>
#include <map>
#include <thread>
#include "rpc.h"

using namespace ERpc;

static const uint16_t kAppNexusUdpPort = 31851;
static const size_t kAppEventLoopMs = 2000;
static const uint8_t kAppServerAppTid = 100;
static const uint8_t kAppClientAppTid = 200;
static const uint8_t kAppReqType = 3;

/* Shared between client and server thread */
std::atomic<bool> server_ready; /* Client starts after server is ready */
std::atomic<bool> client_done;  /* Server ends after client is done */

const uint8_t phy_port = 0;
const size_t numa_node = 0;
char local_hostname[kMaxHostnameLen];

struct app_context_t {
  bool is_client;
  Rpc<IBTransport> *rpc;
};

void req_handler(const MsgBuffer *req_msgbuf, app_resp_t *app_resp,
                 void *_context) {
  assert(req_msgbuf != nullptr);
  assert(app_resp != nullptr);
  assert(_context != nullptr);
  auto *context = (app_context_t *)_context;
  assert(!context->is_client);
}

void resp_handler(const MsgBuffer *req_msgbuf, const MsgBuffer *resp_msgbuf,
                  void *_context) {
  assert(req_msgbuf != nullptr);
  assert(resp_msgbuf != nullptr);
  assert(_context != nullptr);
  auto *context = (app_context_t *)_context;
  assert(context->is_client);
}

void sm_hander(Session *session, SessionMgmtEventType sm_event_type,
               SessionMgmtErrType sm_err_type, void *_context) {
  _unused(session);

  auto *context = (app_context_t *)_context;
  assert(context->is_client);
  if (sm_event_type == SessionMgmtEventType::kConnected) {
    assert(sm_err_type == SessionMgmtErrType::kNoError);
    // Do something
  } else {
    // Do something else
  }
}

/* The server thread used by all tests */
void server_thread_func(Nexus *nexus, uint8_t app_tid) {
  Rpc<IBTransport> rpc(nexus, nullptr, app_tid, &sm_hander, phy_port,
                       numa_node);
  rpc.register_ops(kAppReqType, Ops(req_handler, resp_handler));

  server_ready = true;

  while (!client_done) { /* Wait for the client */
    rpc.run_event_loop_timeout(kAppEventLoopMs);
  }

  /* The client is done after disconnecting */
  ASSERT_EQ(rpc.num_active_sessions(), 0);
}

void simple_small_msg(Nexus *nexus) {
  while (!server_ready) { /* Wait for server */
    usleep(1);
  }

  auto *context = new app_context_t();
  Rpc<IBTransport> rpc(nexus, (void *)context, kAppClientAppTid, &sm_hander,
                       phy_port, numa_node);

  /* Connect the session */
  Session *session =
      rpc.create_session(local_hostname, kAppServerAppTid, phy_port);

  rpc.run_event_loop_timeout(kAppEventLoopMs);

  ASSERT_EQ(session->state, SessionState::kConnected);

  /* Disconnect the session */
  rpc.destroy_session(session);
  rpc.run_event_loop_timeout(kAppEventLoopMs);

  client_done = true;
}

TEST(SimpleSmallMsg, SimpleSmallMsg) {
  Nexus nexus(kAppNexusUdpPort, .8);
  server_ready = false;
  client_done = false;

  std::thread server_thread(server_thread_func, &nexus, kAppServerAppTid);
  std::thread client_thread(simple_small_msg, &nexus);
  server_thread.join();
  client_thread.join();
}

int main(int argc, char **argv) {
  Nexus::get_hostname(local_hostname);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

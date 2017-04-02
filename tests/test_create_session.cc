#include "test_basics.h"

class AppContext : public BasicAppContext {
 public:
  SessionMgmtErrType exp_err;
  int session_num;
};

/* Only invoked for clients */
void test_sm_handler(int session_num, SessionMgmtEventType sm_event_type,
                     SessionMgmtErrType sm_err_type, void *_context) {
  ASSERT_TRUE(_context != nullptr);
  AppContext *context = (AppContext *)_context;
  context->num_sm_resps++;

  /* Check that the error type matches the expected value */
  ASSERT_EQ(sm_err_type, context->exp_err);
  ASSERT_EQ(session_num, context->session_num);

  /* If the error type is really an error, the event should be connect failed */
  if (sm_err_type == SessionMgmtErrType::kNoError) {
    ASSERT_EQ(sm_event_type, SessionMgmtEventType::kConnected);
  } else {
    ASSERT_EQ(sm_event_type, SessionMgmtEventType::kConnectFailed);
  }
}

//
// Test: Successful connection establishment
//
void simple_connect(Nexus<IBTransport> *nexus, size_t) {
  /* We're testing session connection, so can't use client_connect_sessions */
  while (!server_ready) { /* Wait for server */
    usleep(1);
  }

  AppContext context;
  context.rpc =
      new Rpc<IBTransport>(nexus, (void *)&context, kAppClientAppTid,
                           &test_sm_handler, kAppPhyPort, kAppNumaNode);

  /* Connect the session */
  context.exp_err = SessionMgmtErrType::kNoError;
  context.session_num = context.rpc->create_session(
      local_hostname, kAppServerAppTid, kAppPhyPort);
  ASSERT_GE(context.session_num, 0);

  context.rpc->run_event_loop_timeout(kAppEventLoopMs);
  ASSERT_EQ(context.num_sm_resps, 1);

  /* Free resources */
  delete context.rpc;
  client_done = true;
}

TEST(SuccessfulConnect, SuccessfulConnect) {
  launch_server_client_threads(1, 0, simple_connect, basic_empty_req_handler);
}

//
// Create (and connect) a session with an invalid remote port. The server should
// reply with the error code
//
void invalid_remote_port(Nexus<IBTransport> *nexus, size_t) {
  /* We're testing session connection, so can't use client_connect_sessions */
  while (!server_ready) { /* Wait for server */
    usleep(1);
  }

  AppContext context;
  context.rpc =
      new Rpc<IBTransport>(nexus, (void *)&context, kAppClientAppTid,
                           &test_sm_handler, kAppPhyPort, kAppNumaNode);

  /* Connect the session */
  context.exp_err = SessionMgmtErrType::kInvalidRemotePort;
  context.session_num = context.rpc->create_session(
      local_hostname, kAppServerAppTid, kAppPhyPort + 1);
  ASSERT_GE(context.session_num, 0); /* Local session creation works */

  context.rpc->run_event_loop_timeout(kAppEventLoopMs);
  ASSERT_EQ(context.num_sm_resps, 1);

  /* Free resources */
  delete context.rpc;
  client_done = true;
}

TEST(InvalidRemotePort, InvalidRemotePort) {
  launch_server_client_threads(1, 0, invalid_remote_port,
                               basic_empty_req_handler);
}

int main(int argc, char **argv) {
  /* We don't have disconnection logic here */
  server_check_all_disconnected = false;

  Nexus<IBTransport>::get_hostname(local_hostname);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

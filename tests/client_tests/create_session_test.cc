#include "client_tests.h"

class AppContext : public BasicAppContext {
 public:
  SmErrType exp_err;
  int session_num;
};

// Only invoked for clients
void test_sm_handler(int session_num, SmEventType sm_event_type,
                     SmErrType sm_err_type, void *_context) {
  AppContext *context = static_cast<AppContext *>(_context);
  context->num_sm_resps++;

  // Check that the error type matches the expected value
  ASSERT_EQ(sm_err_type, context->exp_err);
  ASSERT_EQ(session_num, context->session_num);

  // If the error type is really an error, the event should be connect failed
  if (sm_err_type == SmErrType::kNoError) {
    ASSERT_EQ(sm_event_type, SmEventType::kConnected);
  } else {
    ASSERT_EQ(sm_event_type, SmEventType::kConnectFailed);
  }
}

//
// Test: Successful connection establishment
//
void simple_connect(Nexus *nexus, size_t) {
  // We're testing session connection, so can't use client_connect_sessions
  AppContext context;
  context.rpc = new Rpc<IBTransport>(nexus, static_cast<void *>(&context),
                                     kTestClientRpcId, &test_sm_handler,
                                     kTestPhyPort, kTestNumaNode);

  // Connect the session
  context.exp_err = SmErrType::kNoError;
  context.session_num =
      context.rpc->create_session("localhost", kTestServerRpcId, kTestPhyPort);
  ASSERT_GE(context.session_num, 0);

  context.rpc->run_event_loop(kTestEventLoopMs);
  ASSERT_EQ(context.num_sm_resps, 1);

  // Free resources
  delete context.rpc;
  client_done = true;
}

TEST(Base, SimpleConnect) {
  auto reg_info_vec = {ReqFuncRegInfo(kTestReqType, basic_empty_req_handler,
                                      ReqFuncType::kForeground)};

  launch_server_client_threads(1, 0, simple_connect, reg_info_vec,
                               ConnectServers::kFalse, 0.0);
}

//
// Create (and connect) a session with an invalid remote port. The server should
// reply with the error code
//
void invalid_remote_port(Nexus *nexus, size_t) {
  // We're testing session connection, so can't use client_connect_sessions
  AppContext context;
  context.rpc = new Rpc<IBTransport>(nexus, static_cast<void *>(&context),
                                     kTestClientRpcId, &test_sm_handler,
                                     kTestPhyPort, kTestNumaNode);

  // Connect the session
  context.exp_err = SmErrType::kInvalidRemotePort;
  context.session_num = context.rpc->create_session(
      "localhost", kTestServerRpcId, kTestPhyPort + 1);
  ASSERT_GE(context.session_num, 0);  // Local session creation works

  context.rpc->run_event_loop(kTestEventLoopMs);
  ASSERT_EQ(context.num_sm_resps, 1);

  // Free resources
  delete context.rpc;
  client_done = true;
}

TEST(Base, InvalidRemotePort) {
  auto reg_info_vec = {ReqFuncRegInfo(kTestReqType, basic_empty_req_handler,
                                      ReqFuncType::kForeground)};

  launch_server_client_threads(1, 0, invalid_remote_port, reg_info_vec,
                               ConnectServers::kFalse, 0.0);
}

int main(int argc, char **argv) {
  // We don't have disconnection logic here
  server_check_all_disconnected = false;
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

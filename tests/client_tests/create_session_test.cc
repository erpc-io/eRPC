#include "client_tests.h"

class AppContext : public BasicAppContext {
 public:
  SmErrType exp_err_;
  int session_num_;
};

// Only invoked for clients
void test_sm_handler(int session_num, SmEventType sm_event_type,
                     SmErrType sm_err_type, void *_c) {
  AppContext *c = static_cast<AppContext *>(_c);
  c->num_sm_resps_++;

  // Check that the error type matches the expected value
  ASSERT_EQ(sm_err_type, c->exp_err_);
  ASSERT_EQ(session_num, c->session_num_);

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
  AppContext c;
  c.rpc_ = new Rpc<CTransport>(nexus, static_cast<void *>(&c), kTestClientRpcId,
                               &test_sm_handler, kTestClientPhyPort);

  // Connect the session
  c.exp_err_ = SmErrType::kNoError;
  c.session_num_ = c.rpc_->create_session("127.0.0.1:31850", kTestServerRpcId);
  ASSERT_GE(c.session_num_, 0);

  c.rpc_->run_event_loop(kTestEventLoopMs);
  ASSERT_EQ(c.num_sm_resps_, 1);

  // Free resources
  delete c.rpc_;
  client_done = true;
}

TEST(Base, SimpleConnect) {
  auto reg_info_vec = {ReqFuncRegInfo(kTestReqType, basic_empty_req_handler,
                                      ReqFuncType::kForeground)};

  launch_server_client_threads(1, 0, simple_connect, reg_info_vec,
                               ConnectServers::kFalse, 0.0);
}

int main(int argc, char **argv) {
  // We don't have disconnection logic here
  server_check_all_disconnected = false;
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#include "client_tests.h"

/// This test uses a request handler that is never invoked
auto reg_info_vec = {ReqFuncRegInfo(kTestReqType, basic_empty_req_handler,
                                    ReqFuncType::kForeground)};

/// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  SmEventType exp_event_;
  SmErrType exp_err_;

  /// Fill in the values expected in the next session management callback
  void arm(SmEventType exp_event, SmErrType exp_err) {
    num_sm_resps_ = 0;  // Reset
    this->exp_event_ = exp_event;
    this->exp_err_ = exp_err;
  }
};

/// The common session management handler for all subtests
void sm_handler(int session_num, SmEventType sm_event_type,
                SmErrType sm_err_type, void *_c) {
  _unused(session_num);

  AppContext *c = static_cast<AppContext *>(_c);
  c->num_sm_resps_++;
  printf("sm_handler: num_sm_resps = %zu\n", c->num_sm_resps_);

  // Check that the event and error types matche their expected values
  ASSERT_EQ(sm_event_type, c->exp_event_);
  ASSERT_EQ(sm_err_type, c->exp_err_);
}

///
/// Simple successful disconnection of one session, and other simple tests
///
void simple_disconnect(Nexus *nexus, size_t) {
  // We're testing session connection, so can't use client_connect_sessions
  AppContext c;
  c.rpc_ = new Rpc<CTransport>(nexus, static_cast<void *>(&c), kTestClientRpcId,
                              &sm_handler, kTestClientPhyPort);
  auto *rpc = c.rpc_;

  // Create the session
  int session_num = rpc->create_session("127.0.0.1:31850", kTestServerRpcId);
  ASSERT_GE(session_num, 0);
  ASSERT_NE(rpc->destroy_session(session_num), 0);  // Try early disconnect

  // Connect the session
  c.arm(SmEventType::kConnected, SmErrType::kNoError);
  wait_for_sm_resps_or_timeout(c, 1);
  ASSERT_EQ(c.num_sm_resps_, 1);  // The connect event

  // Disconnect the session
  c.arm(SmEventType::kDisconnected, SmErrType::kNoError);
  rpc->destroy_session(session_num);
  wait_for_sm_resps_or_timeout(c, 1);
  ASSERT_EQ(c.num_sm_resps_, 1);  // The disconnect event
  ASSERT_EQ(rpc->num_active_sessions(), 0);

  // Other simple tests

  // Try to disconnect the session again. This should fail.
  ASSERT_NE(rpc->destroy_session(session_num), 0);

  // Try to disconnect an invalid session number. This should fail.
  ASSERT_NE(rpc->destroy_session(-1), 0);

  delete c.rpc_;
  client_done = true;
}

TEST(Base, SimpleDisconnect) {
  launch_server_client_threads(1, 0, simple_disconnect, reg_info_vec,
                               ConnectServers::kFalse, 0.0);
}

///
/// Repeat: Create as many sessions to the server as possible and disconnect
/// them all.
///
void disconnect_multi(Nexus *nexus, size_t) {
  // We're testing session connection, so can't use client_connect_sessions()
  AppContext c;
  c.rpc_ = new Rpc<CTransport>(nexus, static_cast<void *>(&c), kTestClientRpcId,
                              &sm_handler, kTestClientPhyPort);
  auto *rpc = c.rpc_;

  // The number of sessions we can create before running out of ring buffers
  size_t num_sessions = Transport::kNumRxRingEntries / kSessionCredits;
  c.session_num_arr_ = new int[num_sessions];

  for (size_t iter = 0; iter < 3; iter++) {
    for (size_t i = 0; i < num_sessions; i++) {
      int session_num =
          rpc->create_session("127.0.0.1:31850", kTestServerRpcId);
      ASSERT_GE(session_num, 0);
      c.session_num_arr_[i] = session_num;
    }

    // Try to create one more session. This should fail.
    int session_num = rpc->create_session("127.0.0.1:31850", kTestServerRpcId);
    ASSERT_LT(session_num, 0);

    // Connect the sessions
    c.arm(SmEventType::kConnected, SmErrType::kNoError);
    wait_for_sm_resps_or_timeout(c, num_sessions);
    ASSERT_EQ(c.num_sm_resps_, num_sessions);  // The connect events

    // Disconnect the sessions
    c.arm(SmEventType::kDisconnected, SmErrType::kNoError);

    for (size_t i = 0; i < num_sessions; i++) {
      rpc->destroy_session(c.session_num_arr_[i]);
    }

    wait_for_sm_resps_or_timeout(c, num_sessions);
    ASSERT_EQ(c.num_sm_resps_, num_sessions);  // The disconnect events

    ASSERT_EQ(rpc->num_active_sessions(), 0);
  }

  delete c.rpc_;
  client_done = true;
}

TEST(Base, DisconnectMulti) {
  launch_server_client_threads(1, 0, disconnect_multi, reg_info_vec,
                               ConnectServers::kFalse, 0.0);
}

///
/// Disconnect a session that encountered a remote error. This should succeed.
///
void disconnect_remote_error(Nexus *nexus, size_t) {
  // We're testing session connection, so can't use client_connect_sessions
  AppContext c;
  c.rpc_ = new Rpc<CTransport>(nexus, static_cast<void *>(&c), kTestClientRpcId,
                              &sm_handler, kTestClientPhyPort);
  auto *rpc = c.rpc_;

  // Create a session that uses an invalid remote port
  int session_num =
      rpc->create_session("127.0.0.1:31850", kTestServerRpcId + 1);
  ASSERT_GE(session_num, 0);
  c.arm(SmEventType::kConnectFailed, SmErrType::kInvalidRemoteRpcId);
  wait_for_sm_resps_or_timeout(c, 1);
  ASSERT_EQ(c.num_sm_resps_, 1);  // The connect failed event

  // After invoking the kConnectFailed callback, the Rpc event loop immediately
  // buries the session since there are no server resources to free.
  ASSERT_EQ(rpc->num_active_sessions(), 0);

  delete c.rpc_;
  client_done = true;
}

TEST(Base, DisconnectRemoteError) {
  launch_server_client_threads(1, 0, disconnect_remote_error, reg_info_vec,
                               ConnectServers::kFalse, 0.0);
}

///
/// Create a session for which the client fails to resolve the server's routing
/// info while processing the connect response.
///
void disconnect_local_error(Nexus *nexus, size_t) {
  // We're testing session connection, so can't use client_connect_sessions
  AppContext c;
  c.rpc_ = new Rpc<CTransport>(nexus, static_cast<void *>(&c), kTestClientRpcId,
                              &sm_handler, kTestClientPhyPort);
  auto *rpc = c.rpc_;

  // Force Rpc to fail remote routing info resolution at client
  rpc->fault_inject_fail_resolve_rinfo_st();

  int session_num = rpc->create_session("127.0.0.1:31850", kTestServerRpcId);
  ASSERT_GE(session_num, 0);

  c.arm(SmEventType::kDisconnected, SmErrType::kNoError);
  wait_for_sm_resps_or_timeout(c, 1);
  ASSERT_EQ(c.num_sm_resps_, 1);  // The connect failed event

  // After invoking the kConnectFailed callback, the Rpc event loop tries to
  // free session resources at the server. This does not invoke a callback on
  // completion, so just wait for the callback-less freeing to complete.
  rpc->run_event_loop(kTestEventLoopMs);
  ASSERT_EQ(rpc->num_active_sessions(), 0);

  delete c.rpc_;
  client_done = true;
}

TEST(Base, DisconnectLocalError) {
  launch_server_client_threads(1, 0, disconnect_local_error, reg_info_vec,
                               ConnectServers::kFalse, 0.0);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

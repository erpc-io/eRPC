#include "test_basics.h"

/// This test uses a request handler that is never invoked
auto reg_info_vec = {ReqFuncRegInfo(kAppReqType, basic_empty_req_handler,
                                    ReqFuncType::kForeground)};

/// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  SmEventType exp_event;
  SmErrType exp_err;

  /// Fill in the values expected in the next session management callback
  void arm(SmEventType exp_event, SmErrType exp_err) {
    num_sm_resps = 0;  // Reset
    this->exp_event = exp_event;
    this->exp_err = exp_err;
  }
};

/// The common session management handler for all subtests
void sm_handler(int session_num, SmEventType sm_event_type,
                SmErrType sm_err_type, void *_context) {
  _unused(session_num);

  AppContext *context = static_cast<AppContext *>(_context);
  context->num_sm_resps++;
  printf("sm_handler: num_sm_resps = %zu\n", context->num_sm_resps);

  // Check that the event and error types matche their expected values
  ASSERT_EQ(sm_event_type, context->exp_event);
  ASSERT_EQ(sm_err_type, context->exp_err);
}

///
/// Simple successful disconnection of one session, and other simple tests
///
void simple_disconnect(Nexus<IBTransport> *nexus, size_t) {
  // We're testing session connection, so can't use client_connect_sessions
  AppContext context;
  context.rpc = new Rpc<IBTransport>(nexus, static_cast<void *>(&context),
                                     kAppClientRpcId, &sm_handler, kAppPhyPort,
                                     kAppNumaNode);
  auto *rpc = context.rpc;

  // Create the session
  int session_num =
      rpc->create_session("localhost", kAppServerRpcId, kAppPhyPort);
  ASSERT_GE(session_num, 0);
  ASSERT_NE(rpc->destroy_session(session_num), 0);  // Try early disconnect

  // Connect the session
  context.arm(SmEventType::kConnected, SmErrType::kNoError);
  wait_for_sm_resps_or_timeout(context, 1, nexus->freq_ghz);
  ASSERT_EQ(context.num_sm_resps, 1);  // The connect event

  // Disconnect the session
  context.arm(SmEventType::kDisconnected, SmErrType::kNoError);
  rpc->destroy_session(session_num);
  wait_for_sm_resps_or_timeout(context, 1, nexus->freq_ghz);
  ASSERT_EQ(context.num_sm_resps, 1);  // The disconnect event
  ASSERT_EQ(rpc->num_active_sessions(), 0);

  // Other simple tests

  // Try to disconnect the session again. This should fail.
  ASSERT_NE(rpc->destroy_session(session_num), 0);

  // Try to disconnect an invalid session number. This should fail.
  ASSERT_NE(rpc->destroy_session(-1), 0);

  delete context.rpc;
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
void disconnect_multi(Nexus<IBTransport> *nexus, size_t) {
  // We're testing session connection, so can't use client_connect_sessions()
  AppContext context;
  context.rpc = new Rpc<IBTransport>(nexus, static_cast<void *>(&context),
                                     kAppClientRpcId, &sm_handler, kAppPhyPort,
                                     kAppNumaNode);
  auto *rpc = context.rpc;

  // The number of sessions we can create before running out of RECVs
  size_t num_sessions = Transport::kRecvQueueDepth / Session::kSessionCredits;
  context.session_num_arr = new int[num_sessions];

  for (size_t iter = 0; iter < 3; iter++) {
    for (size_t i = 0; i < num_sessions; i++) {
      int session_num =
          rpc->create_session("localhost", kAppServerRpcId, kAppPhyPort);
      ASSERT_GE(session_num, 0);
      context.session_num_arr[i] = session_num;
    }

    // Try to create one more session. This should fail.
    int session_num =
        rpc->create_session("localhost", kAppServerRpcId, kAppPhyPort);
    ASSERT_LT(session_num, 0);

    // Connect the sessions
    context.arm(SmEventType::kConnected, SmErrType::kNoError);
    wait_for_sm_resps_or_timeout(context, num_sessions, nexus->freq_ghz);
    ASSERT_EQ(context.num_sm_resps, num_sessions);  // The connect events

    // Disconnect the sessions
    context.arm(SmEventType::kDisconnected, SmErrType::kNoError);

    for (size_t i = 0; i < num_sessions; i++) {
      rpc->destroy_session(context.session_num_arr[i]);
    }

    wait_for_sm_resps_or_timeout(context, num_sessions, nexus->freq_ghz);
    ASSERT_EQ(context.num_sm_resps, num_sessions);  // The disconnect events

    ASSERT_EQ(rpc->num_active_sessions(), 0);
  }

  delete context.rpc;
  client_done = true;
}

TEST(Base, DisconnectMulti) {
  launch_server_client_threads(1, 0, disconnect_multi, reg_info_vec,
                               ConnectServers::kFalse, 0.0);
}

///
/// Disconnect a session that encountered a remote error. This should succeed.
///
void disconnect_remote_error(Nexus<IBTransport> *nexus, size_t) {
  // We're testing session connection, so can't use client_connect_sessions
  AppContext context;
  context.rpc = new Rpc<IBTransport>(nexus, static_cast<void *>(&context),
                                     kAppClientRpcId, &sm_handler, kAppPhyPort,
                                     kAppNumaNode);
  auto *rpc = context.rpc;

  // Create a session that uses an invalid remote port
  int session_num =
      rpc->create_session("localhost", kAppServerRpcId, kAppPhyPort + 1);
  ASSERT_GE(session_num, 0);
  context.arm(SmEventType::kConnectFailed, SmErrType::kInvalidRemotePort);
  wait_for_sm_resps_or_timeout(context, 1, nexus->freq_ghz);
  ASSERT_EQ(context.num_sm_resps, 1);  // The connect failed event

  // After invoking the kConnectFailed callback, the Rpc event loop immediately
  // buries the session since there are no server resources to free.
  ASSERT_EQ(rpc->num_active_sessions(), 0);

  delete context.rpc;
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
void disconnect_local_error(Nexus<IBTransport> *nexus, size_t) {
  // We're testing session connection, so can't use client_connect_sessions
  AppContext context;
  context.rpc = new Rpc<IBTransport>(nexus, static_cast<void *>(&context),
                                     kAppClientRpcId, &sm_handler, kAppPhyPort,
                                     kAppNumaNode);
  auto *rpc = context.rpc;

  // Force Rpc to fail remote routing info resolution at client
  rpc->fault_inject_fail_resolve_server_rinfo_st();

  int session_num =
      rpc->create_session("localhost", kAppServerRpcId, kAppPhyPort);
  ASSERT_GE(session_num, 0);

  context.arm(SmEventType::kDisconnected, SmErrType::kNoError);
  wait_for_sm_resps_or_timeout(context, 1, nexus->freq_ghz);
  ASSERT_EQ(context.num_sm_resps, 1);  // The connect failed event

  // After invoking the kConnectFailed callback, the Rpc event loop tries to
  // free session resources at the server. This does not invoke a callback on
  // completion, so just wait for the callback-less freeing to complete.
  rpc->run_event_loop(kAppEventLoopMs);
  ASSERT_EQ(rpc->num_active_sessions(), 0);

  delete context.rpc;
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

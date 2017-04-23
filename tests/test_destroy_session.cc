#include "test_basics.h"

/// This test uses a request handler that is never invoked
auto reg_info_vec = {ReqFuncRegInfo(kAppReqType, basic_empty_req_handler,
                                    ReqFuncType::kFgTerminal)};

/// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  SessionMgmtEventType exp_event;
  SessionMgmtErrType exp_err;
  int exp_session_num;

  /// Fill in the values expected in the next session management callback
  void arm(SessionMgmtEventType exp_event, SessionMgmtErrType exp_err,
           int exp_session_num) {
    num_sm_resps = 0; /* Reset */
    this->exp_event = exp_event;
    this->exp_err = exp_err;
    this->exp_session_num = exp_session_num;
  }
};

/// The common session management handler for all subtests
void sm_handler(int session_num, SessionMgmtEventType sm_event_type,
                SessionMgmtErrType sm_err_type, void *_context) {
  _unused(session_num);

  AppContext *context = static_cast<AppContext *>(_context);
  context->num_sm_resps++;
  printf("sm_handler: num_sm_resps = %zu\n", context->num_sm_resps);

  /* Check that the event and error types matche their expected values */
  ASSERT_EQ(sm_event_type, context->exp_event);
  ASSERT_EQ(sm_err_type, context->exp_err);
  ASSERT_EQ(session_num, context->exp_session_num);
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

  /* Create the session */
  int session_num =
      rpc->create_session(local_hostname, kAppServerRpcId, kAppPhyPort);
  ASSERT_GE(session_num, 0);
  ASSERT_NE(rpc->destroy_session(session_num), 0); /* Try early disconnect */

  /* Connect the session */
  context.arm(SessionMgmtEventType::kConnected, SessionMgmtErrType::kNoError,
              session_num);
  wait_for_sm_resps_or_timeout(context, 1, nexus->freq_ghz);
  ASSERT_EQ(context.num_sm_resps, 1); /* The connect event */

  /* Disconnect the session */
  context.arm(SessionMgmtEventType::kDisconnected, SessionMgmtErrType::kNoError,
              session_num);
  rpc->destroy_session(session_num);
  wait_for_sm_resps_or_timeout(context, 1, nexus->freq_ghz);
  ASSERT_EQ(context.num_sm_resps, 1); /* The disconnect event */
  ASSERT_EQ(rpc->num_active_sessions(), 0);

  // Other simple tests

  /* Try to disconnect the session again. This should fail. */
  ASSERT_NE(rpc->destroy_session(session_num), 0);

  /* Try to disconnect an invalid session number. This should fail. */
  ASSERT_NE(rpc->destroy_session(-1), 0);

  delete context.rpc;
  client_done = true;
}

TEST(SimpleDisconnect, SimpleDisconnect) {
  launch_server_client_threads(1, 0, simple_disconnect, reg_info_vec,
                               ConnectServers::kFalse);
}

///
/// Repeat: Create a session to the server and disconnect it.
///
void disconnect_multi(Nexus<IBTransport> *nexus, size_t) {
  // We're testing session connection, so can't use client_connect_sessions
  AppContext context;
  context.rpc = new Rpc<IBTransport>(nexus, static_cast<void *>(&context),
                                     kAppClientRpcId, &sm_handler, kAppPhyPort,
                                     kAppNumaNode);
  auto *rpc = context.rpc;

  for (size_t i = 0; i < 3; i++) {
    int session_num =
        rpc->create_session(local_hostname, kAppServerRpcId, kAppPhyPort);
    ASSERT_GE(session_num, 0);

    /* Connect the session */
    context.arm(SessionMgmtEventType::kConnected, SessionMgmtErrType::kNoError,
                session_num);
    wait_for_sm_resps_or_timeout(context, 1, nexus->freq_ghz);
    ASSERT_EQ(context.num_sm_resps, 1); /* The connect event */

    /* Disconnect the session */
    context.arm(SessionMgmtEventType::kDisconnected,
                SessionMgmtErrType::kNoError, session_num);
    rpc->destroy_session(session_num);
    wait_for_sm_resps_or_timeout(context, 1, nexus->freq_ghz);
    ASSERT_EQ(context.num_sm_resps, 1); /* The disconnect event */

    ASSERT_EQ(rpc->num_active_sessions(), 0);
  }

  delete context.rpc;
  client_done = true;
}

TEST(DisconnectMulti, DisconnectMulti) {
  launch_server_client_threads(1, 0, disconnect_multi, reg_info_vec,
                               ConnectServers::kFalse);
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

  /* Create a session that uses an invalid remote port */
  int session_num =
      rpc->create_session(local_hostname, kAppServerRpcId, kAppPhyPort + 1);
  ASSERT_GE(session_num, 0);
  context.arm(SessionMgmtEventType::kConnectFailed,
              SessionMgmtErrType::kInvalidRemotePort, session_num);
  wait_for_sm_resps_or_timeout(context, 1, nexus->freq_ghz);
  ASSERT_EQ(context.num_sm_resps, 1); /* The connect failed event */

  /*
   * After invoking the kConnectFailed callback, the Rpc event loop immediately
   * buries the session since there are no server resources to free.
   */
  ASSERT_EQ(rpc->num_active_sessions(), 0);

  delete context.rpc;
  client_done = true;
}

TEST(DisconnectRemoteError, DisconnectRemoteError) {
  launch_server_client_threads(1, 0, disconnect_remote_error, reg_info_vec,
                               ConnectServers::kFalse);
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
  rpc->flt_inj_resolve_server_rinfo = true;

  int session_num =
      rpc->create_session(local_hostname, kAppServerRpcId, kAppPhyPort);
  context.arm(SessionMgmtEventType::kConnectFailed,
              SessionMgmtErrType::kRoutingResolutionFailure, session_num);
  wait_for_sm_resps_or_timeout(context, 1, nexus->freq_ghz);
  ASSERT_EQ(context.num_sm_resps, 1); /* The connect failed event */

  /*
   * After invoking the kConnectFailed callback, the Rpc event loop tries to
   * free session resources at the server. This does not invoke a callback on
   * completion, so just wait for the callback-less freeing to complete.
   */
  rpc->run_event_loop_timeout(kAppEventLoopMs);
  ASSERT_EQ(rpc->num_active_sessions(), 0);

  delete context.rpc;
  client_done = true;
}

TEST(DisconnectLocalError, DisconnectLocalError) {
  launch_server_client_threads(1, 0, disconnect_local_error, reg_info_vec,
                               ConnectServers::kFalse);
}

int main(int argc, char **argv) {
  Nexus<IBTransport>::get_hostname(local_hostname);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

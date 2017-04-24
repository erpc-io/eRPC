/**
 * @file test_api_restrictions.cc
 * @brief Test the restrictions on the eRPC API.
 */

#include "test_basics.h"

static constexpr size_t kAppReqSize = 32;  ///< Request size for this test

/// Per-thread application context
class AppContext : public BasicAppContext {};

enum class AppDeathMode {
  kReqHandlerRunsEventLoop,
  kReqHandlerDeletesRpc,
  kContFuncRunsEventLoop,
  kContFuncDeletesRpc
};

/// Used to configure the cause of death of the req handler or continuation
std::atomic<AppDeathMode> app_death_mode;

void req_handler(ReqHandle *, void *);  // Forward declaration
auto reg_info_vec = {
    ReqFuncRegInfo(kAppReqType, req_handler, ReqFuncType::kBackground)};

/// A request handler with a configurable death mode
void req_handler(ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr && _context != nullptr);

  auto *context = static_cast<AppContext *>(_context);
  assert(!context->is_client);
  assert(context->rpc->in_background());

  // Try to create a session
  int session_num = context->rpc->create_session(local_hostname,
                                                 kAppServerRpcId, kAppPhyPort);
  ASSERT_EQ(session_num, -EPERM);

  // Try to destroy a valid session number
  int ret = context->rpc->destroy_session(0);
  ASSERT_EQ(ret, -EPERM);

  if (app_death_mode == AppDeathMode::kReqHandlerRunsEventLoop) {
    // Try to run the event loop
    if (kDatapathChecks) {
      test_printf("test: Trying to run event loop in req handler.\n");
      ASSERT_THROW(context->rpc->run_event_loop_one(), std::runtime_error);
    }
  }

  if (app_death_mode == AppDeathMode::kReqHandlerDeletesRpc) {
    // Try to delete the Rpc. This crashes even without kDatapathChecks.
    test_printf("test: Trying to delete Rpc in req handler.\n");
    ASSERT_DEATH(delete context->rpc, ".*");
  }

  Rpc<IBTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                      kAppReqSize);
  req_handle->prealloc_used = true;
  context->rpc->enqueue_response(req_handle);
}

/// A continuation function with a configurable death mode
void cont_func(RespHandle *resp_handle, void *_context, size_t) {
  assert(resp_handle != nullptr && _context != nullptr);
  auto *context = static_cast<AppContext *>(_context);
  assert(context->is_client);

  if (app_death_mode == AppDeathMode::kContFuncRunsEventLoop) {
    // Try to run the event loop
    if (kDatapathChecks) {
      test_printf("test: Trying to run event loop in cont func.\n");
      ASSERT_THROW(context->rpc->run_event_loop_one(), std::runtime_error);
    }
  }

  if (app_death_mode == AppDeathMode::kContFuncDeletesRpc) {
    // Try to delete the Rpc. This crashes even without kDatapathChecks.
    test_printf("test: Trying to delete Rpc in cont func.\n");
    ASSERT_DEATH(delete context->rpc, ".*");
  }

  context->num_rpc_resps++;
  context->rpc->release_respone(resp_handle);
}

/// The test function
void test_func(Nexus<IBTransport> *nexus, size_t num_sessions) {
  /* Create the Rpc and connect the session */
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions, basic_sm_handler);

  Rpc<IBTransport> *rpc = context.rpc;
  int session_num = context.session_num_arr[0];

  /* Send a message */
  MsgBuffer req_msgbuf = rpc->alloc_msg_buffer(kAppReqSize);
  assert(req_msgbuf.buf != nullptr);

  int ret =
      rpc->enqueue_request(session_num, kAppReqType, &req_msgbuf, cont_func, 0);
  _unused(ret);
  assert(ret == 0);

  wait_for_rpc_resps_or_timeout(context, 1, nexus->freq_ghz);
  assert(context.num_rpc_resps == 1);

  rpc->free_msg_buffer(req_msgbuf);

  /* Disconnect the session */
  rpc->destroy_session(session_num);
  rpc->run_event_loop_timeout(kAppEventLoopMs);

  /* Free resources */
  delete rpc;
  client_done = true;
}

/// A helper function to run the tests below. GTest's death test seems to
/// keep running a copy of the program after the crash, which cleans up
/// hugepages.
void TEST_HELPER() {
  launch_server_client_threads(1, 1, test_func, reg_info_vec,
                               ConnectServers::kFalse);
}

TEST(Restrictions, ReqHandlerRunsEventLoop) {
  app_death_mode = AppDeathMode::kReqHandlerRunsEventLoop;
  TEST_HELPER();
}

TEST(Restrictions, ReqHandlerDeletesRpc) {
  app_death_mode = AppDeathMode::kReqHandlerDeletesRpc;
  TEST_HELPER();
}

TEST(Restrictions, ContFuncRunsEventLoop) {
  app_death_mode = AppDeathMode::kContFuncRunsEventLoop;
  TEST_HELPER();
}

TEST(Restrictions, ContFuncDeletesRpc) {
  app_death_mode = AppDeathMode::kContFuncDeletesRpc;
  TEST_HELPER();
}

int main(int argc, char **argv) {
  Nexus<IBTransport>::get_hostname(local_hostname);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/**
 * @file test_api_restrictions.cc
 * @brief Test the restrictions on the eRPC API.
 */

#include "client_tests.h"

static constexpr size_t kTestReqSize = 32;  ///< Request size for this test

/// Per-thread application context
class AppContext : public BasicAppContext {};

enum class AppDeathMode { kBgThreadDeletesRpc };

/// Used to configure the cause of death of the req handler or continuation
std::atomic<AppDeathMode> app_death_mode;

void req_handler(ReqHandle *, void *);  // Forward declaration
auto reg_info_vec = {
    ReqFuncRegInfo(kTestReqType, req_handler, ReqFuncType::kBackground)};

/// A request handler with a configurable death mode
void req_handler(ReqHandle *req_handle, void *_context) {
  auto *context = static_cast<AppContext *>(_context);
  assert(!context->is_client);
  assert(context->rpc->in_background());

  // Try to create a session
  int session_num = context->rpc->create_session(
      "localhost:31850", kTestServerRpcId, kTestPhyPort);
  ASSERT_EQ(session_num, -EPERM);

  // Try to destroy a valid session number
  int ret = context->rpc->destroy_session(0);
  ASSERT_EQ(ret, -EPERM);

  if (app_death_mode == AppDeathMode::kBgThreadDeletesRpc) {
    test_printf("test: Trying to delete Rpc in background thread.\n");
    ASSERT_DEATH(delete context->rpc, ".*");
  }

  Rpc<CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf,
                                     kTestReqSize);
  req_handle->prealloc_used = true;
  context->rpc->enqueue_response(req_handle);
}

/// A continuation function with a configurable death mode
void cont_func(RespHandle *resp_handle, void *_context, size_t) {
  auto *context = static_cast<AppContext *>(_context);
  assert(context->is_client);
  assert(!context->rpc->in_background());

  context->num_rpc_resps++;
  context->rpc->release_response(resp_handle);
}

/// The test function
void test_func(Nexus *nexus, size_t num_sessions) {
  // Create the Rpc and connect the session
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions, basic_sm_handler);

  Rpc<CTransport> *rpc = context.rpc;
  int session_num = context.session_num_arr[0];

  // Send a message
  MsgBuffer req_msgbuf = rpc->alloc_msg_buffer(kTestReqSize);
  assert(req_msgbuf.buf != nullptr);

  MsgBuffer resp_msgbuf = rpc->alloc_msg_buffer(kTestReqSize);
  assert(resp_msgbuf.buf != nullptr);

  // Run continuation in foreground thread
  int ret = rpc->enqueue_request(session_num, kTestReqType, &req_msgbuf,
                                 &resp_msgbuf, cont_func, 0);
  _unused(ret);
  assert(ret == 0);

  wait_for_rpc_resps_or_timeout(context, 1, nexus->freq_ghz);
  assert(context.num_rpc_resps == 1);

  rpc->free_msg_buffer(req_msgbuf);

  // Disconnect the session
  rpc->destroy_session(session_num);
  rpc->run_event_loop(kTestEventLoopMs);

  // Free resources
  delete rpc;
  client_done = true;
}

/// A helper function to run the tests below. GTest's death test seems to
/// keep running a copy of the program after the crash, which cleans up
/// hugepages.
void TEST_HELPER() {
  // Run one background thread to run the request handler; continuation runs
  // in the foreground.
  launch_server_client_threads(1, 1, test_func, reg_info_vec,
                               ConnectServers::kFalse, 0.0);
}

TEST(Restrictions, BgThreadDeletesRpc) {
  app_death_mode = AppDeathMode::kBgThreadDeletesRpc;
  TEST_HELPER();
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

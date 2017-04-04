#include "test_basics.h"

/// Per-thread application context
class AppContext : public BasicAppContext {};

/// The common request handler for all subtests. Copies the request string to
/// the response.
void req_handler(ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr);
  assert(_context != nullptr);

  auto *context = (AppContext *)_context;
  ASSERT_FALSE(context->is_client);
  ASSERT_TRUE(context->rpc->in_background());

  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  test_printf("Server: Received request %s\n", req_msgbuf->buf);

  /* Try to create a session */
  int session_num = context->rpc->create_session(local_hostname,
                                                 kAppServerAppTid, kAppPhyPort);
  ASSERT_EQ(session_num, -EPERM);

  /* Try to destroy a valid session number */
  int ret = context->rpc->destroy_session(0);
  ASSERT_EQ(ret, -EPERM);

  /*
   * Try to run the event loop. This may not cause a crash if asserts are
   * disabled.
   */
  ASSERT_DEATH(context->rpc->run_event_loop_one(), ".*");
  ASSERT_DEATH(assert(false), ".*");

  size_t resp_size = strlen((char *)req_msgbuf->buf);
  Rpc<IBTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf, resp_size);
  strcpy((char *)req_handle->pre_resp_msgbuf.buf, (char *)req_msgbuf->buf);
  req_handle->prealloc_used = true;

  context->rpc->enqueue_response(req_handle);
}

/// The common continuation function for all subtests. This checks that the
/// request buffer is identical to the response buffer, and increments the
/// number of responses in the context.
void cont_func(RespHandle *resp_handle, void *_context, size_t tag) {
  assert(resp_handle != nullptr);
  assert(_context != nullptr);

  const MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  test_printf("Client: Received response %s, tag = %zu\n",
              (char *)resp_msgbuf->buf, tag);

  std::string exp_resp = std::string("APP_MSG-") + std::to_string(tag);
  ASSERT_STREQ((char *)resp_msgbuf->buf, exp_resp.c_str());

  auto *context = (AppContext *)_context;
  ASSERT_TRUE(context->is_client);
  context->num_rpc_resps++;

  context->rpc->release_respone(resp_handle);
}

///
/// Test: Send one small request packet to the invalid request handler
///
void one_small_rpc(Nexus<IBTransport> *nexus, size_t num_sessions) {
  /* Create the Rpc and connect the session */
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions, basic_sm_handler);

  Rpc<IBTransport> *rpc = context.rpc;
  int session_num = context.session_num_arr[0];

  /* Send a message */
  MsgBuffer req_msgbuf = rpc->alloc_msg_buffer(strlen("APP_MSG-0") + 1);
  ASSERT_NE(req_msgbuf.buf, nullptr);
  strcpy((char *)req_msgbuf.buf, "APP_MSG-0");

  test_printf("test: Sending request %s\n", (char *)req_msgbuf.buf);
  int ret =
      rpc->enqueue_request(session_num, kAppReqType, &req_msgbuf, cont_func, 0);
  if (ret != 0) {
    test_printf("test: enqueue_request error %s\n", std::strerror(ret));
  }
  ASSERT_EQ(ret, 0);

  wait_for_rpc_resps_or_timeout(context, 1, nexus->freq_ghz);
  ASSERT_EQ(context.num_rpc_resps, 1);

  rpc->free_msg_buffer(req_msgbuf);

  /* Disconnect the session */
  rpc->destroy_session(session_num);
  rpc->run_event_loop_timeout(kAppEventLoopMs);

  /* Free resources */
  delete rpc;
  client_done = true;
}

TEST(BgRestrictions, All) {
  auto reg_info_vec = {
      ReqFuncRegInfo(kAppReqType, req_handler, ReqFuncType::kBackground)};

  // One background thread
  launch_server_client_threads(1, 1, one_small_rpc, reg_info_vec,
                               ConnectServers::kFalse);
}

int main(int argc, char **argv) {
  Nexus<IBTransport>::get_hostname(local_hostname);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

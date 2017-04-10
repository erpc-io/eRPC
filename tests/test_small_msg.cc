#include "test_basics.h"

void req_handler(ReqHandle *, void *);  // Forward declaration

/// Request handler for foreground testing
auto reg_info_vec_fg = {
    ReqFuncRegInfo(kAppReqType, req_handler, ReqFuncType::kFgTerminal)};

/// Request handler for background testing
auto reg_info_vec_bg = {
    ReqFuncRegInfo(kAppReqType, req_handler, ReqFuncType::kBackground)};

/// Per-thread application context
class AppContext : public BasicAppContext {};

/// The common request handler for all subtests. Copies the request string to
/// the response.
void req_handler(ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr);
  assert(_context != nullptr);

  auto *context = static_cast<AppContext *>(_context);
  ASSERT_FALSE(context->is_client);

  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  test_printf("Server: Received request %s\n", req_msgbuf->buf);

  size_t resp_size = req_msgbuf->get_data_size();
  Rpc<IBTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf, resp_size);
  strcpy(reinterpret_cast<char *>(req_handle->pre_resp_msgbuf.buf),
         reinterpret_cast<char *>(req_msgbuf->buf));
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
              reinterpret_cast<char *>(resp_msgbuf->buf), tag);

  std::string exp_resp = std::string("APP_MSG-") + std::to_string(tag);
  ASSERT_STREQ(reinterpret_cast<char *>(resp_msgbuf->buf), exp_resp.c_str());

  auto *context = static_cast<AppContext *>(_context);
  ASSERT_TRUE(context->is_client);
  context->num_rpc_resps++;

  context->rpc->release_respone(resp_handle);
}

///
/// Test: Send one small request packet and check that we receive the
/// correct response
///
void one_small_rpc(Nexus<IBTransport> *nexus, size_t num_sessions) {
  // Create the Rpc and connect the session
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions, basic_sm_handler);

  Rpc<IBTransport> *rpc = context.rpc;
  int session_num = context.session_num_arr[0];

  // Send a message
  MsgBuffer req_msgbuf = rpc->alloc_msg_buffer(strlen("APP_MSG-0") + 1);
  ASSERT_NE(req_msgbuf.buf, nullptr);
  strcpy(reinterpret_cast<char *>(req_msgbuf.buf), "APP_MSG-0");

  test_printf("test: Sending request %s\n",
              reinterpret_cast<char *>(req_msgbuf.buf));
  int ret =
      rpc->enqueue_request(session_num, kAppReqType, &req_msgbuf, cont_func, 0);
  if (ret != 0) {
    test_printf("test: enqueue_request error %s\n", std::strerror(ret));
  }
  ASSERT_EQ(ret, 0);

  wait_for_rpc_resps_or_timeout(context, 1, nexus->freq_ghz);
  ASSERT_EQ(context.num_rpc_resps, 1);

  rpc->free_msg_buffer(req_msgbuf);

  // Disconnect the session
  rpc->destroy_session(session_num);
  rpc->run_event_loop_timeout(kAppEventLoopMs);

  // Free resources
  delete rpc;
  client_done = true;
}

TEST(OneSmallRpc, Foreground) {
  launch_server_client_threads(1, 0, one_small_rpc, reg_info_vec_fg,
                               ConnectServers::kFalse);
}

TEST(OneSmallRpc, Background) {
  // One background thread
  launch_server_client_threads(1, 1, one_small_rpc, reg_info_vec_bg,
                               ConnectServers::kFalse);
}

///
/// Test: Repeat: Multiple small Rpcs on one session
///
void multi_small_rpc_one_session(Nexus<IBTransport> *nexus,
                                 size_t num_sessions) {
  // Create the Rpc and connect the session
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions, basic_sm_handler);

  Rpc<IBTransport> *rpc = context.rpc;
  int session_num = context.session_num_arr[0];

  // Pre-create MsgBuffers so we can test reuse and resizing
  MsgBuffer req_msgbuf[Session::kSessionReqWindow];
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    req_msgbuf[i] = rpc->alloc_msg_buffer(rpc->get_max_data_per_pkt());
    ASSERT_NE(req_msgbuf[i].buf, nullptr);
  }

  size_t req_suffix = 0; // The integer suffix after every request message

  for (size_t iter = 0; iter < 2; iter++) {
    context.num_rpc_resps = 0;

    // Enqueue as many requests as one session allows
    for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
      std::string req_msg =
          std::string("APP_MSG-") + std::to_string(req_suffix);
      rpc->resize_msg_buffer(&req_msgbuf[i], req_msg.length() + 1);

      strcpy(reinterpret_cast<char *>(req_msgbuf[i].buf), req_msg.c_str());

      test_printf("test: Sending request %s\n",
                  reinterpret_cast<char *>(req_msgbuf[i].buf));
      int ret = rpc->enqueue_request(session_num, kAppReqType, &req_msgbuf[i],
                                     cont_func, req_suffix);
      if (ret != 0) {
        test_printf("test: enqueue_request error %s\n", std::strerror(ret));
      }
      ASSERT_EQ(ret, 0);

      req_suffix++;
    }

    // Try to enqueue one more request - this should fail
    int ret = rpc->enqueue_request(session_num, kAppReqType, &req_msgbuf[0],
                                   cont_func, 0);
    ASSERT_NE(ret, 0);

    wait_for_rpc_resps_or_timeout(context, Session::kSessionReqWindow,
                                  nexus->freq_ghz);
    ASSERT_EQ(context.num_rpc_resps, Session::kSessionReqWindow);
  }

  // Free the request MsgBuffers
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    rpc->free_msg_buffer(req_msgbuf[i]);
  }

  // Disconnect the session
  rpc->destroy_session(session_num);
  rpc->run_event_loop_timeout(kAppEventLoopMs);

  // Free resources
  delete rpc;
  client_done = true;
}

TEST(MultiSmallRpcOneSession, Foreground) {
  launch_server_client_threads(1, 0, multi_small_rpc_one_session,
                               reg_info_vec_fg, ConnectServers::kFalse);
}

TEST(MultiSmallRpcOneSession, Background) {
  // 2 background threads
  launch_server_client_threads(1, 2, multi_small_rpc_one_session,
                               reg_info_vec_bg, ConnectServers::kFalse);
}

///
/// Test: Repeat: Multiple small Rpcs on multiple sessions
///
void multi_small_rpc_multi_session(Nexus<IBTransport> *nexus,
                                   size_t num_sessions) {
  // Create the Rpc and connect the session
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions, basic_sm_handler);

  Rpc<IBTransport> *rpc = context.rpc;
  int *session_num_arr = context.session_num_arr;

  // Pre-create MsgBuffers so we can test reuse and resizing
  size_t tot_reqs_per_iter = num_sessions * Session::kSessionReqWindow;
  MsgBuffer req_msgbuf[tot_reqs_per_iter];
  for (size_t req_i = 0; req_i < tot_reqs_per_iter; req_i++) {
    req_msgbuf[req_i] = rpc->alloc_msg_buffer(rpc->get_max_data_per_pkt());
    ASSERT_NE(req_msgbuf[req_i].buf, nullptr);
  }

  size_t req_suffix = 0; // The integer suffix after every request message
  for (size_t iter = 0; iter < 5; iter++) {
    context.num_rpc_resps = 0;

    test_printf("Client: Iteration %zu.\n", iter);

    for (size_t sess_i = 0; sess_i < num_sessions; sess_i++) {
      // Enqueue as many requests as this session allows
      for (size_t w_i = 0; w_i < Session::kSessionReqWindow; w_i++) {
        size_t req_i = (sess_i * Session::kSessionReqWindow) + w_i;
        assert(req_i < tot_reqs_per_iter);

        std::string req_msg =
            std::string("APP_MSG-") + std::to_string(req_suffix);
        rpc->resize_msg_buffer(&req_msgbuf[req_i], req_msg.length() + 1);

        strcpy(reinterpret_cast<char *>(req_msgbuf[req_i].buf),
               req_msg.c_str());

        test_printf("test: Sending request %s\n",
                    reinterpret_cast<char *>(req_msgbuf[req_i].buf));

        int ret =
            rpc->enqueue_request(session_num_arr[sess_i], kAppReqType,
                                 &req_msgbuf[req_i], cont_func, req_suffix);
        if (ret != 0) {
          test_printf("Client: enqueue_request error %s\n", std::strerror(ret));
        }
        ASSERT_EQ(ret, 0);

        req_suffix++;
      }
    }

    wait_for_rpc_resps_or_timeout(context, tot_reqs_per_iter, nexus->freq_ghz);
    ASSERT_EQ(context.num_rpc_resps, tot_reqs_per_iter);
  }

  // Free the request MsgBuffers
  for (size_t req_i = 0; req_i < tot_reqs_per_iter; req_i++) {
    rpc->free_msg_buffer(req_msgbuf[req_i]);
  }

  // Disconnect the sessions
  for (size_t sess_i = 0; sess_i < num_sessions; sess_i++) {
    rpc->destroy_session(session_num_arr[sess_i]);
  }

  rpc->run_event_loop_timeout(kAppEventLoopMs);

  // Free resources
  delete rpc;
  client_done = true;
}

TEST(MultiSmallRpcMultiSession, Foreground) {
  // Use enough sessions to exceed the Rpc's unexpected window
  size_t num_sessions =
      (Rpc<IBTransport>::kRpcUnexpPktWindow / Session::kSessionReqWindow) + 2;
  launch_server_client_threads(num_sessions, 0, multi_small_rpc_multi_session,
                               reg_info_vec_fg, ConnectServers::kFalse);
}

TEST(MultiSmallRpcMultiSession, Background) {
  // Use enough sessions to exceed the Rpc's unexpected window
  size_t num_sessions =
      (Rpc<IBTransport>::kRpcUnexpPktWindow / Session::kSessionReqWindow) + 2;
  // 3 background threads
  launch_server_client_threads(num_sessions, 3, multi_small_rpc_multi_session,
                               reg_info_vec_bg, ConnectServers::kFalse);
}

int main(int argc, char **argv) {
  Nexus<IBTransport>::get_hostname(local_hostname);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

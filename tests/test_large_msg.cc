#include "test_basics.h"

static constexpr size_t kAppMinMsgSize =
    Rpc<IBTransport>::max_data_per_pkt() + 1; /* At least 2 packets */

/// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  FastRand fastrand;  ///< Used for picking large message sizes
};

/// Pick a random message size with at least two packets
size_t pick_large_msg_size(AppContext *app_context) {
  assert(app_context != nullptr);
  uint32_t sample = app_context->fastrand.next_u32();
  uint32_t msg_size =
      sample % (Rpc<IBTransport>::kMaxMsgSize - kAppMinMsgSize) +
      kAppMinMsgSize;

  assert(msg_size >= kAppMinMsgSize &&
         msg_size <= Rpc<IBTransport>::kMaxMsgSize);
  return (size_t)msg_size;
}

/// The common request handler for all subtests. Copies the request string to
/// the response.
void req_handler(ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr);
  assert(_context != nullptr);

  auto *context = (AppContext *)_context;
  ASSERT_FALSE(context->is_client);

  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  size_t req_size = req_msgbuf->get_data_size();

  req_handle->prealloc_used = false;

  /* MsgBuffer allocation is thread safe */
  req_handle->dyn_resp_msgbuf = context->rpc->alloc_msg_buffer(req_size);
  ASSERT_NE(req_handle->dyn_resp_msgbuf.buf, nullptr);
  size_t user_alloc_tot = context->rpc->get_stat_user_alloc_tot();

  memcpy((char *)req_handle->dyn_resp_msgbuf.buf, (char *)req_msgbuf->buf,
         req_size);

  test_printf(
      "Server: Received request of length %zu. "
      "Rpc memory used = %zu bytes (%.3f MB)\n",
      req_size, user_alloc_tot, (double)user_alloc_tot / MB(1));

  context->rpc->enqueue_response(req_handle);
}

/// The common continuation function for all subtests. This checks that the
/// request buffer is identical to the response buffer, and increments the
/// number of responses in the context.
void cont_func(RespHandle *resp_handle, void *_context, size_t tag) {
  assert(resp_handle != nullptr);
  assert(_context != nullptr);
  _unused(tag);

  const MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  test_printf("Client: Received response of length %zu.\n",
              (char *)resp_msgbuf->get_data_size());

  /*
  ASSERT_EQ(req_msgbuf->get_data_size(), resp_msgbuf->get_data_size());
  ASSERT_STREQ((char *)req_msgbuf->buf, (char *)resp_msgbuf->buf);
  */

  auto *context = (AppContext *)_context;
  ASSERT_TRUE(context->is_client);
  context->num_rpc_resps++;

  context->rpc->release_respone(resp_handle);
}

///
/// Test: Send one large request message and check that we receive the
/// correct response
///
void one_large_rpc(Nexus<IBTransport> *nexus, size_t num_sessions = 1) {
  /* Create the Rpc and connect the session */
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions, basic_sm_handler);

  Rpc<IBTransport> *rpc = context.rpc;
  int session_num = context.session_num_arr[0];

  /* Send a message */
  size_t req_size = kAppMinMsgSize;
  MsgBuffer req_msgbuf = rpc->alloc_msg_buffer(req_size);
  ASSERT_NE(req_msgbuf.buf, nullptr);

  for (size_t i = 0; i < req_size; i++) {
    req_msgbuf.buf[i] = 'a';
  }
  req_msgbuf.buf[req_size - 1] = 0;

  test_printf("Client: Sending request of size %zu\n", req_size);
  int ret =
      rpc->enqueue_request(session_num, kAppReqType, &req_msgbuf, cont_func, 0);
  if (ret != 0) {
    test_printf("Client: enqueue_request error %s\n", std::strerror(ret));
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

TEST(OneLargeRpc, Foreground) {
  launch_server_client_threads(1, 0, one_large_rpc, req_handler,
                               ConnectServers::kFalse);
}

TEST(OneLargeRpc, Background) {
  /* One background thread */
  launch_server_client_threads(1, 1, one_large_rpc, req_handler,
                               ConnectServers::kFalse);
}

///
/// Test: Repeat: Multiple large Rpcs on one session, with random size
///
void multi_large_rpc_one_session(Nexus<IBTransport> *nexus,
                                 size_t num_sessions = 1) {
  /* Create the Rpc and connect the session */
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions, basic_sm_handler);

  Rpc<IBTransport> *rpc = context.rpc;
  int session_num = context.session_num_arr[0];

  /* Pre-create MsgBuffers so we can test reuse and resizing */
  MsgBuffer req_msgbuf[Session::kSessionReqWindow];
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    req_msgbuf[i] = rpc->alloc_msg_buffer(Rpc<IBTransport>::kMaxMsgSize);
    ASSERT_NE(req_msgbuf[i].buf, nullptr);
  }

  for (size_t iter = 0; iter < 2; iter++) {
    context.num_rpc_resps = 0;

    /* Enqueue as many requests as one session allows */
    for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
      size_t req_len = pick_large_msg_size((AppContext *)&context);
      rpc->resize_msg_buffer(&req_msgbuf[i], req_len);

      for (size_t j = 0; j < req_len; j++) {
        req_msgbuf[i].buf[j] = 'a' + ((i + j) % 26);
      }
      req_msgbuf[i].buf[req_len - 1] = 0;

      test_printf("Client: Sending request of length = %zu\n", req_len);
      int ret = rpc->enqueue_request(session_num, kAppReqType, &req_msgbuf[i],
                                     cont_func, 0);
      if (ret != 0) {
        test_printf("Client: enqueue_request error %s\n", std::strerror(ret));
      }
      ASSERT_EQ(ret, 0);
    }

    /* Try to enqueue one more request - this should fail */
    int ret = rpc->enqueue_request(session_num, kAppReqType, &req_msgbuf[0],
                                   cont_func, 0);
    ASSERT_NE(ret, 0);

    wait_for_rpc_resps_or_timeout(context, Session::kSessionReqWindow,
                                  nexus->freq_ghz);
    ASSERT_EQ(context.num_rpc_resps, Session::kSessionReqWindow);
  }

  /* Free the request MsgBuffers */
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    rpc->free_msg_buffer(req_msgbuf[i]);
  }

  /* Disconnect the session */
  rpc->destroy_session(session_num);
  rpc->run_event_loop_timeout(kAppEventLoopMs);

  /* Free resources */
  delete rpc;
  client_done = true;
}

TEST(MultiLargeRpcOneSession, Foreground) {
  launch_server_client_threads(1, 0, multi_large_rpc_one_session, req_handler,
                               ConnectServers::kFalse);
}

TEST(MultiLargeRpcOneSession, Background) {
  /* 2 background threads */
  launch_server_client_threads(1, 2, multi_large_rpc_one_session, req_handler,
                               ConnectServers::kFalse);
}

///
/// Test: Repeat: Multiple large Rpcs on multiple sessions
///
void multi_large_rpc_multi_session(Nexus<IBTransport> *nexus,
                                   size_t num_sessions) {
  /* Create the Rpc and connect the session */
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions, basic_sm_handler);

  Rpc<IBTransport> *rpc = context.rpc;
  int *session_num_arr = context.session_num_arr;

  /* Pre-create MsgBuffers so we can test reuse and resizing */
  size_t tot_reqs_per_iter = num_sessions * Session::kSessionReqWindow;
  MsgBuffer req_msgbuf[tot_reqs_per_iter];
  for (size_t req_i = 0; req_i < tot_reqs_per_iter; req_i++) {
    req_msgbuf[req_i] = rpc->alloc_msg_buffer(Rpc<IBTransport>::kMaxMsgSize);
    ASSERT_NE(req_msgbuf[req_i].buf, nullptr);
  }

  for (size_t iter = 0; iter < 5; iter++) {
    context.num_rpc_resps = 0;

    for (size_t sess_i = 0; sess_i < num_sessions; sess_i++) {
      /* Enqueue as many requests as this session allows */
      for (size_t w_i = 0; w_i < Session::kSessionReqWindow; w_i++) {
        size_t req_i = (sess_i * Session::kSessionReqWindow) + w_i;
        assert(req_i < tot_reqs_per_iter);

        size_t req_len = pick_large_msg_size((AppContext *)&context);
        rpc->resize_msg_buffer(&req_msgbuf[req_i], req_len);

        for (size_t j = 0; j < req_len; j++) {
          req_msgbuf[req_i].buf[j] = 'a' + ((req_i + j) % 26);
        }
        req_msgbuf[req_i].buf[req_len - 1] = 0;

        test_printf("Client: Sending request of length = %zu\n", req_len);

        int ret = rpc->enqueue_request(session_num_arr[sess_i], kAppReqType,
                                       &req_msgbuf[req_i], cont_func, 0);
        if (ret != 0) {
          test_printf("Client: enqueue_request error %s\n", std::strerror(ret));
        }
        ASSERT_EQ(ret, 0);
      }
    }

    wait_for_rpc_resps_or_timeout(context, tot_reqs_per_iter, nexus->freq_ghz);
    ASSERT_EQ(context.num_rpc_resps, tot_reqs_per_iter);
  }

  /* Free the request MsgBuffers */
  for (size_t req_i = 0; req_i < tot_reqs_per_iter; req_i++) {
    rpc->free_msg_buffer(req_msgbuf[req_i]);
  }

  /* Disconnect the sessions */
  for (size_t sess_i = 0; sess_i < num_sessions; sess_i++) {
    rpc->destroy_session(session_num_arr[sess_i]);
  }

  rpc->run_event_loop_timeout(kAppEventLoopMs);

  /* Free resources */
  delete rpc;
  client_done = true;
}

TEST(MultiLargeRpcMultiSession, Foreground) {
  assert(!kDatapathVerbose);
  /* Use enough sessions to exceed the Rpc's unexpected window */
  size_t num_sessions =
      (Rpc<IBTransport>::kRpcUnexpPktWindow / Session::kSessionReqWindow) + 2;
  launch_server_client_threads(num_sessions, 0, multi_large_rpc_multi_session,
                               req_handler, ConnectServers::kFalse);
}

TEST(MultiLargeRpcMultiSession, Background) {
  assert(!kDatapathVerbose);
  /* Use enough sessions to exceed the Rpc's unexpected window */
  size_t num_sessions =
      (Rpc<IBTransport>::kRpcUnexpPktWindow / Session::kSessionReqWindow) + 2;
  /* 3 background threads */
  launch_server_client_threads(num_sessions, 3, multi_large_rpc_multi_session,
                               req_handler, ConnectServers::kFalse);
}

///
/// Test: Repeat: Multiple large Rpcs on multiple sessions, trying to force
/// a memory leak. This test takes a long time so it's disabled by default.
///
void memory_leak(Nexus<IBTransport> *nexus, size_t num_sessions) {
  /* Create the Rpc and connect the session */
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions, basic_sm_handler);

  Rpc<IBTransport> *rpc = context.rpc;
  int *session_num_arr = context.session_num_arr;

  /* Run many iterations to stress memory leaks */
  for (size_t iter = 0; iter < 500; iter++) {
    test_printf("Client: Iteration %zu\n", iter);

    /* Create new MsgBuffers in each iteration to stress leaks */
    size_t tot_reqs_per_iter = num_sessions * Session::kSessionReqWindow;
    MsgBuffer req_msgbuf[tot_reqs_per_iter];
    for (size_t req_i = 0; req_i < tot_reqs_per_iter; req_i++) {
      req_msgbuf[req_i] = rpc->alloc_msg_buffer(Rpc<IBTransport>::kMaxMsgSize);
      ASSERT_NE(req_msgbuf[req_i].buf, nullptr);
    }

    context.num_rpc_resps = 0;

    for (size_t sess_i = 0; sess_i < num_sessions; sess_i++) {
      /* Enqueue as many requests as this session allows */
      for (size_t w_i = 0; w_i < Session::kSessionReqWindow; w_i++) {
        size_t req_i = (sess_i * Session::kSessionReqWindow) + w_i;
        assert(req_i < tot_reqs_per_iter);

        size_t req_len = pick_large_msg_size((AppContext *)&context);
        rpc->resize_msg_buffer(&req_msgbuf[req_i], req_len);

        for (size_t j = 0; j < req_len; j++) {
          req_msgbuf[req_i].buf[j] = 'a' + ((req_i + j) % 26);
        }
        req_msgbuf[req_i].buf[req_len - 1] = 0;

        test_printf("Client: Iter %zu: Sending request of length = %zu\n", iter,
                    req_len);

        int ret = rpc->enqueue_request(session_num_arr[sess_i], kAppReqType,
                                       &req_msgbuf[req_i], cont_func, 0);
        if (ret != 0) {
          test_printf("Client: enqueue_request error %s\n", std::strerror(ret));
        }
        ASSERT_EQ(ret, 0);
      }
    }

    /* Run the event loop for up to kAppMaxEventLoopMs milliseconds */
    wait_for_rpc_resps_or_timeout(context, tot_reqs_per_iter, nexus->freq_ghz);
    ASSERT_EQ(context.num_rpc_resps, tot_reqs_per_iter);

    /* Free the request MsgBuffers */
    for (size_t req_i = 0; req_i < tot_reqs_per_iter; req_i++) {
      rpc->free_msg_buffer(req_msgbuf[req_i]);
    }
  }

  /* Disconnect the sessions */
  for (size_t sess_i = 0; sess_i < num_sessions; sess_i++) {
    rpc->destroy_session(session_num_arr[sess_i]);
  }

  rpc->run_event_loop_timeout(kAppEventLoopMs);

  /* Free resources */
  delete rpc;
  client_done = true;
}

TEST(DISABLED_MemoryLeak, Foreground) {
  assert(!kDatapathVerbose);
  /* Use enough sessions to exceed the Rpc's unexpected window */
  size_t num_sessions =
      (Rpc<IBTransport>::kRpcUnexpPktWindow / Session::kSessionReqWindow) + 2;
  launch_server_client_threads(num_sessions, 0, memory_leak, req_handler,
                               ConnectServers::kFalse);
}

TEST(DISABLED_MemoryLeak, Background) {
  assert(!kDatapathVerbose);
  /* Use enough sessions to exceed the Rpc's unexpected window */
  size_t num_sessions =
      (Rpc<IBTransport>::kRpcUnexpPktWindow / Session::kSessionReqWindow) + 2;
  /* 2 background threads */
  launch_server_client_threads(num_sessions, 2, memory_leak, req_handler,
                               ConnectServers::kFalse);
}

int main(int argc, char **argv) {
  Nexus<IBTransport>::get_hostname(local_hostname);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

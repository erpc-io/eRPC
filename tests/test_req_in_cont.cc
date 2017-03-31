/**
 * @file test_nested_rpc.cc
 * @brief Test issuing requests from within continuations.
 */
#include "test_basics.h"

static constexpr size_t kNumReqs = 100;
static_assert(kNumReqs > Session::kSessionReqWindow, "");

/// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  FastRand fast_rand;
  MsgBuffer *req_msgbuf;
};

/// Pick a random message size (>= 1 byte)
size_t get_rand_msg_size(AppContext *app_context) {
  assert(app_context != nullptr);
  uint32_t sample = app_context->fast_rand.next_u32();
  uint32_t msg_size = sample % Rpc<IBTransport>::kMaxMsgSize;
  if (msg_size == 0) {
    msg_size = 1;
  }

  return (size_t)msg_size;
}

void req_handler(ReqHandle *req_handle, const MsgBuffer *req_msgbuf,
                 void *_context) {
  assert(req_handle != nullptr);
  assert(req_msgbuf != nullptr);
  assert(_context != nullptr);

  auto *context = (AppContext *)_context;
  ASSERT_FALSE(context->is_client);

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

void cont_func(RespHandle *, const MsgBuffer *, void *, size_t);  // Fwd decl
void enqueue_requests_helper(AppContext *context, size_t num_reqs) {
  for (size_t i = 0; i < num_reqs; i++) {
    size_t req_size = get_rand_msg_size(context);
    test_printf("Client: Sending request of size %zu\n", req_size);

    context->rpc->resize_msg_buffer(context->req_msgbuf, req_size);
    int ret =
        context->rpc->enqueue_request(context->session_num_arr[0], kAppReqType,
                                      context->req_msgbuf, cont_func, req_size);
    ASSERT_EQ(ret, 0);
  }
}

void cont_func(RespHandle *resp_handle, const MsgBuffer *resp_msgbuf,
               void *_context, size_t tag) {
  assert(resp_handle != nullptr);
  assert(resp_msgbuf != nullptr);
  assert(_context != nullptr);
  _unused(tag);

  auto *context = (AppContext *)_context;
  ASSERT_TRUE(context->is_client);

  test_printf("Client: Received response %zu of length %zu.\n",
              context->num_rpc_resps, (char *)resp_msgbuf->get_data_size());

  ASSERT_EQ(resp_msgbuf->get_data_size(), tag);

  context->num_rpc_resps++;
  context->rpc->release_respone(resp_handle);

  enqueue_requests_helper(context, 1);
}

void client_thread(Nexus *nexus, size_t num_sessions = 1) {
  // Create the Rpc and connect the session
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions, basic_sm_handler);

  Rpc<IBTransport> *rpc = context.rpc;

  MsgBuffer req_msgbuf = rpc->alloc_msg_buffer(Rpc<IBTransport>::kMaxMsgSize);
  ASSERT_NE(req_msgbuf.buf, nullptr);
  context.req_msgbuf = &req_msgbuf;

  size_t req_size = get_rand_msg_size(&context);
  rpc->resize_msg_buffer(&req_msgbuf, req_size);
  for (size_t i = 0; i < req_size; i++) {
    req_msgbuf.buf[i] = (uint8_t)req_size;
  }

  // Start by filling the request window
  enqueue_requests_helper(&context, Session::kSessionReqWindow);

  client_wait_for_rpc_resps_or_timeout(nexus, context, kNumReqs);
  ASSERT_EQ(context.num_rpc_resps, kNumReqs);

  rpc->free_msg_buffer(req_msgbuf);

  // Disconnect the session
  rpc->destroy_session(context.session_num_arr[0]);
  rpc->run_event_loop_timeout(kAppEventLoopMs);

  // Free resources
  delete rpc;
  client_done = true;
}

TEST(SendReqInCont, Foreground) {
  launch_server_client_threads(1, 0, client_thread, req_handler);
}

int main(int argc, char **argv) {
  Nexus::get_hostname(local_hostname);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/**
 * @file test_nested_rpc.cc
 * @brief Test issuing requests from within continuations.
 */
#include "test_basics.h"

static constexpr size_t kAppNumReqs = 1000;
static_assert(kAppNumReqs > Session::kSessionReqWindow, "");

union tag_t {
  struct {
    uint32_t req_size;
    uint32_t msgbuf_i;
  };
  size_t tag;
  tag_t(uint32_t req_size, uint32_t msgbuf_i)
      : req_size(req_size), msgbuf_i(msgbuf_i) {}
  tag_t(size_t tag) : tag(tag) {}
};
static_assert(sizeof(tag_t) == sizeof(size_t), "");

/// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  FastRand fast_rand;
  MsgBuffer req_msgbuf[Session::kSessionReqWindow];
  size_t num_reqs_sent = 0;
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

void req_handler(ReqHandle *req_handle, void *_context) {
  assert(req_handle != nullptr);
  assert(_context != nullptr);

  auto *context = (AppContext *)_context;
  ASSERT_FALSE(context->is_client);

  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  size_t req_size = req_msgbuf->get_data_size();

  req_handle->prealloc_used = false;

  // eRPC will free the MsgBuffer
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

void cont_func(RespHandle *, void *, size_t);  // Forward declaration

/// Enqueue a request using the request MsgBuffer with index = msgbuf_i
void enqueue_request_helper(AppContext *context, size_t msgbuf_i) {
  assert(context != nullptr && msgbuf_i < Session::kSessionReqWindow);

  size_t req_size = get_rand_msg_size(context);
  context->rpc->resize_msg_buffer(&context->req_msgbuf[msgbuf_i], req_size);

  tag_t tag((uint32_t)req_size, (uint32_t)msgbuf_i);  // Construct tag

  test_printf("Client: Sending request %zu of size %zu\n",
              context->num_reqs_sent, req_size);

  int ret = context->rpc->enqueue_request(
      context->session_num_arr[0], kAppReqType, &context->req_msgbuf[msgbuf_i],
      cont_func, tag.tag);

  context->num_reqs_sent++;
  ASSERT_EQ(ret, 0);
}

void cont_func(RespHandle *resp_handle, void *_context, size_t tag) {
  assert(resp_handle != nullptr);
  assert(_context != nullptr);
  _unused(tag);

  auto *context = (AppContext *)_context;
  ASSERT_TRUE(context->is_client);

  const MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  test_printf("Client: Received response %zu of length %zu.\n",
              context->num_rpc_resps, (char *)resp_msgbuf->get_data_size());

  ASSERT_EQ(resp_msgbuf->get_data_size(), ((tag_t)tag).req_size);

  context->num_rpc_resps++;
  context->rpc->release_respone(resp_handle);

  if (context->num_rpc_resps < kAppNumReqs) {
    enqueue_request_helper(context, ((tag_t)tag).msgbuf_i);
  }
}

void client_thread(Nexus<IBTransport> *nexus, size_t num_sessions) {
  // Create the Rpc and connect the session
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions, basic_sm_handler);

  Rpc<IBTransport> *rpc = context.rpc;

  // Start by filling the request window
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    context.req_msgbuf[i] =
        rpc->alloc_msg_buffer(Rpc<IBTransport>::kMaxMsgSize);
    ASSERT_NE(context.req_msgbuf[i].buf, nullptr);
    enqueue_request_helper(&context, i);
  }

  wait_for_rpc_resps_or_timeout(context, kAppNumReqs, nexus->freq_ghz);
  ASSERT_GE(context.num_rpc_resps, kAppNumReqs);  // We can overshoot a bit

  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    rpc->free_msg_buffer(context.req_msgbuf[i]);
  }

  // Disconnect the sessions
  context.num_sm_resps = 0;
  for (size_t i = 0; i < num_sessions; i++) {
    rpc->destroy_session(context.session_num_arr[0]);
  }
  wait_for_sm_resps_or_timeout(context, num_sessions, nexus->freq_ghz);

  // Free resources
  delete rpc;
  client_done = true;
}

TEST(SendReqInContFunc, Foreground) {
  auto reg_info_vec = {
      ReqFuncRegInfo(kAppReqType, req_handler, ReqFuncType::kFgTerminal)};
  launch_server_client_threads(1, 0, client_thread, reg_info_vec,
                               ConnectServers::kFalse);
}

int main(int argc, char **argv) {
  Nexus<IBTransport>::get_hostname(local_hostname);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

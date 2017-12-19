/**
 * @file test_nested_rpc.cc
 * @brief Test issuing requests from within continuations.
 */
#include "client_tests.h"

static constexpr size_t kTestNumReqs = 1000;
static_assert(kTestNumReqs > Session::kSessionReqWindow, "");
static_assert(kTestNumReqs < std::numeric_limits<uint16_t>::max(), "");

struct tag_t {
  uint16_t req_i;
  uint16_t msgbuf_i;
  uint32_t req_size;
  tag_t(uint16_t req_i, uint16_t msgbuf_i, uint32_t req_size)
      : req_i(req_i), msgbuf_i(msgbuf_i), req_size(req_size) {}
};
static_assert(sizeof(tag_t) == sizeof(size_t), "");

/// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  FastRand fast_rand;
  MsgBuffer req_msgbuf[Session::kSessionReqWindow];
  MsgBuffer resp_msgbuf[Session::kSessionReqWindow];
  size_t num_reqs_sent = 0;
};

void req_handler(ReqHandle *req_handle, void *_context) {
  auto *context = static_cast<AppContext *>(_context);
  assert(!context->is_client);

  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  size_t req_size = req_msgbuf->get_data_size();

  req_handle->prealloc_used = false;

  // eRPC will free the MsgBuffer
  req_handle->dyn_resp_msgbuf = context->rpc->alloc_msg_buffer(req_size);
  assert(req_handle->dyn_resp_msgbuf.buf != nullptr);
  memcpy(req_handle->dyn_resp_msgbuf.buf, req_msgbuf->buf, req_size);

  size_t user_alloc_tot = context->rpc->get_stat_user_alloc_tot();
  test_printf(
      "Server: Received request of length %zu. "
      "Rpc memory used = %zu bytes (%.3f MB)\n",
      req_size, user_alloc_tot, 1.0 * user_alloc_tot / MB(1));

  context->rpc->enqueue_response(req_handle);
}

void cont_func(RespHandle *, void *, size_t);  // Forward declaration

/// Enqueue a request using the request MsgBuffer with index = msgbuf_i
void enqueue_request_helper(AppContext *c, size_t msgbuf_i) {
  assert(msgbuf_i < Session::kSessionReqWindow);

  size_t req_size =
      get_rand_msg_size(&c->fast_rand, c->rpc->get_max_data_per_pkt(),
                        c->rpc->get_max_msg_size());
  c->rpc->resize_msg_buffer(&c->req_msgbuf[msgbuf_i], req_size);

  tag_t tag(static_cast<uint16_t>(c->num_reqs_sent),
            static_cast<uint16_t>(msgbuf_i), static_cast<uint32_t>(req_size));

  test_printf("Client: Sending request %zu of size %zu\n", c->num_reqs_sent,
              req_size);

  int ret = c->rpc->enqueue_request(
      c->session_num_arr[0], kTestReqType, &c->req_msgbuf[msgbuf_i],
      &c->resp_msgbuf[msgbuf_i], cont_func, *reinterpret_cast<size_t *>(&tag));
  _unused(ret);
  assert(ret == 0);

  c->num_reqs_sent++;
}

void cont_func(RespHandle *resp_handle, void *_context, size_t _tag) {
  auto *context = static_cast<AppContext *>(_context);
  assert(context->is_client);
  const MsgBuffer *resp_msgbuf = resp_handle->get_resp_msgbuf();
  auto tag = *reinterpret_cast<tag_t *>(&_tag);

  test_printf("Client: Received response for req %u, length = %zu.\n",
              tag.req_i, resp_msgbuf->get_data_size());

  ASSERT_EQ(resp_msgbuf->get_data_size(), static_cast<tag_t>(tag).req_size);

  context->num_rpc_resps++;
  context->rpc->release_response(resp_handle);

  if (context->num_reqs_sent < kTestNumReqs) {
    enqueue_request_helper(context, static_cast<tag_t>(tag).msgbuf_i);
  }
}

void client_thread(Nexus *nexus, size_t num_sessions) {
  // Create the Rpc and connect the session
  AppContext context;
  client_connect_sessions(nexus, context, num_sessions, basic_sm_handler);

  Rpc<CTransport> *rpc = context.rpc;

  // Start by filling the request window
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    context.req_msgbuf[i] = rpc->alloc_msg_buffer(rpc->get_max_msg_size());
    assert(context.req_msgbuf[i].buf != nullptr);

    context.resp_msgbuf[i] = rpc->alloc_msg_buffer(rpc->get_max_msg_size());
    assert(context.resp_msgbuf[i].buf != nullptr);

    enqueue_request_helper(&context, i);
  }

  wait_for_rpc_resps_or_timeout(context, kTestNumReqs, nexus->freq_ghz);
  assert(context.num_rpc_resps == kTestNumReqs);

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
      ReqFuncRegInfo(kTestReqType, req_handler, ReqFuncType::kForeground)};
  launch_server_client_threads(1, 0, client_thread, reg_info_vec,
                               ConnectServers::kFalse, 0.0);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/**
 * @file test_nested_rpc.cc
 * @brief Test issuing requests from within continuations.
 */
#include "client_tests.h"

static constexpr size_t kTestNumReqs = 1000;
static_assert(kTestNumReqs > kSessionReqWindow, "");
static_assert(kTestNumReqs < std::numeric_limits<uint16_t>::max(), "");

union tag_t {
  struct {
    uint16_t req_i;
    uint16_t msgbuf_i;
    uint32_t req_size;
  } s;
  size_t _tag;

  tag_t(uint16_t req_i, uint16_t msgbuf_i, uint32_t req_size) {
    s.req_i = req_i;
    s.msgbuf_i = msgbuf_i;
    s.req_size = req_size;
  }

  tag_t(size_t _tag) : _tag(_tag) {}
};
static_assert(sizeof(tag_t) == sizeof(void *), "");

/// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  FastRand fast_rand;
  size_t num_reqs_sent = 0;
};

void req_handler(ReqHandle *req_handle, void *_c) {
  auto *c = static_cast<AppContext *>(_c);
  assert(!c->is_client);

  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  size_t req_size = req_msgbuf->get_data_size();

  // eRPC will free the MsgBuffer
  req_handle->dyn_resp_msgbuf = c->rpc->alloc_msg_buffer_or_die(req_size);
  memcpy(req_handle->dyn_resp_msgbuf.buf, req_msgbuf->buf, req_size);

  size_t user_alloc_tot = c->rpc->get_stat_user_alloc_tot();
  test_printf(
      "Server: Received request of length %zu. "
      "Rpc memory used = %zu bytes (%.3f MB)\n",
      req_size, user_alloc_tot, 1.0 * user_alloc_tot / MB(1));

  c->rpc->enqueue_response(req_handle, &req_handle->dyn_resp_msgbuf);
}

void cont_func(void *, void *);  // Forward declaration

/// Enqueue a request using the request MsgBuffer with index = msgbuf_i
void enqueue_request_helper(AppContext *c, size_t msgbuf_i) {
  assert(msgbuf_i < kSessionReqWindow);

  size_t req_size =
      get_rand_msg_size(&c->fast_rand, c->rpc->get_max_data_per_pkt(),
                        c->rpc->get_max_msg_size());
  c->rpc->resize_msg_buffer(&c->req_msgbufs[msgbuf_i], req_size);

  tag_t tag(static_cast<uint16_t>(c->num_reqs_sent),
            static_cast<uint16_t>(msgbuf_i), static_cast<uint32_t>(req_size));

  test_printf("Client: Sending request %zu of size %zu\n", c->num_reqs_sent,
              req_size);

  c->rpc->enqueue_request(c->session_num_arr[0], kTestReqType,
                          &c->req_msgbufs[msgbuf_i], &c->resp_msgbufs[msgbuf_i],
                          cont_func, reinterpret_cast<void *>(tag._tag));

  c->num_reqs_sent++;
}

void cont_func(void *_c, void *_tag) {
  auto *c = static_cast<AppContext *>(_c);
  assert(c->is_client);
  auto tag = *reinterpret_cast<tag_t *>(&_tag);

  const MsgBuffer &resp_msgbuf = c->resp_msgbufs[tag.s.msgbuf_i];
  test_printf("Client: Received response for req %u, length = %zu.\n",
              tag.s.req_i, resp_msgbuf.get_data_size());

  ASSERT_EQ(resp_msgbuf.get_data_size(), static_cast<tag_t>(tag).s.req_size);
  c->num_rpc_resps++;

  if (c->num_reqs_sent < kTestNumReqs) {
    enqueue_request_helper(c, static_cast<tag_t>(tag).s.msgbuf_i);
  }
}

void client_thread(Nexus *nexus, size_t num_sessions) {
  // Create the Rpc and connect the session
  AppContext c;
  client_connect_sessions(nexus, c, num_sessions, basic_sm_handler);

  Rpc<CTransport> *rpc = c.rpc;

  // Start by filling the request window
  c.req_msgbufs.resize(kSessionReqWindow);
  c.resp_msgbufs.resize(kSessionReqWindow);
  for (size_t i = 0; i < kSessionReqWindow; i++) {
    const size_t max_msg_sz = rpc->get_max_msg_size();
    c.req_msgbufs[i] = rpc->alloc_msg_buffer_or_die(max_msg_sz);
    c.resp_msgbufs[i] = rpc->alloc_msg_buffer_or_die(max_msg_sz);
    enqueue_request_helper(&c, i);
  }

  wait_for_rpc_resps_or_timeout(c, kTestNumReqs);
  assert(c.num_rpc_resps == kTestNumReqs);

  for (auto &mb : c.req_msgbufs) rpc->free_msg_buffer(mb);
  for (auto &mb : c.resp_msgbufs) rpc->free_msg_buffer(mb);

  // Disconnect the sessions
  c.num_sm_resps = 0;
  for (size_t i = 0; i < num_sessions; i++) {
    rpc->destroy_session(c.session_num_arr[0]);
  }
  wait_for_sm_resps_or_timeout(c, num_sessions);

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

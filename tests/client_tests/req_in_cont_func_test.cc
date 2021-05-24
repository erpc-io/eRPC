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
    uint16_t req_i_;
    uint16_t msgbuf_i_;
    uint32_t req_size_;
  } s_;
  size_t tag_;

  tag_t(uint16_t req_i, uint16_t msgbuf_i, uint32_t req_size) {
    s_.req_i_ = req_i;
    s_.msgbuf_i_ = msgbuf_i;
    s_.req_size_ = req_size;
  }

  tag_t(size_t _tag) : tag_(_tag) {}
};
static_assert(sizeof(tag_t) == sizeof(void *), "");

/// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  FastRand fast_rand_;
  size_t num_reqs_sent_ = 0;
};

void req_handler(ReqHandle *req_handle, void *_c) {
  auto *c = static_cast<AppContext *>(_c);
  assert(!c->is_client_);

  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  size_t req_size = req_msgbuf->get_data_size();

  // eRPC will free the MsgBuffer
  req_handle->dyn_resp_msgbuf_ = c->rpc_->alloc_msg_buffer_or_die(req_size);
  memcpy(req_handle->dyn_resp_msgbuf_.buf_, req_msgbuf->buf_, req_size);

  size_t user_alloc_tot = c->rpc_->get_stat_user_alloc_tot();
  test_printf(
      "Server: Received request of length %zu. "
      "Rpc memory used = %zu bytes (%.3f MB)\n",
      req_size, user_alloc_tot, 1.0 * user_alloc_tot / MB(1));

  c->rpc_->enqueue_response(req_handle, &req_handle->dyn_resp_msgbuf_);
}

void cont_func(void *, void *);  // Forward declaration

/// Enqueue a request using the request MsgBuffer with index = msgbuf_i
void enqueue_request_helper(AppContext *c, size_t msgbuf_i) {
  assert(msgbuf_i < kSessionReqWindow);

  size_t req_size = get_rand_msg_size(&c->fast_rand_, c->rpc_);
  c->rpc_->resize_msg_buffer(&c->req_msgbufs_[msgbuf_i], req_size);

  tag_t tag(static_cast<uint16_t>(c->num_reqs_sent_),
            static_cast<uint16_t>(msgbuf_i), static_cast<uint32_t>(req_size));

  test_printf("Client: Sending request %zu of size %zu\n", c->num_reqs_sent_,
              req_size);

  c->rpc_->enqueue_request(c->session_num_arr_[0], kTestReqType,
                          &c->req_msgbufs_[msgbuf_i], &c->resp_msgbufs_[msgbuf_i],
                          cont_func, reinterpret_cast<void *>(tag.tag_));

  c->num_reqs_sent_++;
}

void cont_func(void *_c, void *_tag) {
  auto *c = static_cast<AppContext *>(_c);
  assert(c->is_client_);
  auto tag = *reinterpret_cast<tag_t *>(&_tag);

  const MsgBuffer &resp_msgbuf = c->resp_msgbufs_[tag.s_.msgbuf_i_];
  test_printf("Client: Received response for req %u, length = %zu.\n",
              tag.s_.req_i_, resp_msgbuf.get_data_size());

  ASSERT_EQ(resp_msgbuf.get_data_size(), static_cast<tag_t>(tag).s_.req_size_);
  c->num_rpc_resps_++;

  if (c->num_reqs_sent_ < kTestNumReqs) {
    enqueue_request_helper(c, static_cast<tag_t>(tag).s_.msgbuf_i_);
  }
}

void client_thread(Nexus *nexus, size_t num_sessions) {
  // Create the Rpc and connect the session
  AppContext c;
  client_connect_sessions(nexus, c, num_sessions, basic_sm_handler);

  Rpc<CTransport> *rpc = c.rpc_;

  // Start by filling the request window
  c.req_msgbufs_.resize(kSessionReqWindow);
  c.resp_msgbufs_.resize(kSessionReqWindow);
  for (size_t i = 0; i < kSessionReqWindow; i++) {
    const size_t max_msg_sz = rpc->get_max_msg_size();
    c.req_msgbufs_[i] = rpc->alloc_msg_buffer_or_die(max_msg_sz);
    c.resp_msgbufs_[i] = rpc->alloc_msg_buffer_or_die(max_msg_sz);
    enqueue_request_helper(&c, i);
  }

  wait_for_rpc_resps_or_timeout(c, kTestNumReqs);
  assert(c.num_rpc_resps_ == kTestNumReqs);

  for (auto &mb : c.req_msgbufs_) rpc->free_msg_buffer(mb);
  for (auto &mb : c.resp_msgbufs_) rpc->free_msg_buffer(mb);

  // Disconnect the sessions
  c.num_sm_resps_ = 0;
  for (size_t i = 0; i < num_sessions; i++) {
    rpc->destroy_session(c.session_num_arr_[0]);
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

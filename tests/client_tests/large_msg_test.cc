#include "client_tests.h"

void req_handler(ReqHandle *, void *);  // Forward declaration

/// Request handler for foreground testing
auto reg_info_vec_fg = {
    ReqFuncRegInfo(kTestReqType, req_handler, ReqFuncType::kForeground)};

/// Request handler for background testing
auto reg_info_vec_bg = {
    ReqFuncRegInfo(kTestReqType, req_handler, ReqFuncType::kBackground)};

/// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  FastRand fastrand_;  ///< Used for picking large message sizes
};

/// Application-level header carried in each request and response
struct app_hdr_t {
  size_t id_;
  size_t req_size_;
  size_t resp_size_;
  uint8_t byte_contents_;
  uint8_t pad_[7];

  app_hdr_t(size_t req_size, size_t resp_size, uint8_t byte_contents)
      : req_size_(req_size),
        resp_size_(resp_size),
        byte_contents_(byte_contents) {}

  app_hdr_t() {}
};
static_assert(sizeof(app_hdr_t) % sizeof(size_t) == 0, "");

/// Configuration for controlling the test
size_t config_num_iters;         ///< The number of iterations
size_t config_num_sessions;      ///< Number of sessions created by client
size_t config_rpcs_per_session;  ///< Number of Rpcs per session per iteration
size_t config_num_bg_threads;    ///< Number of background threads

/// The common request handler for all subtests
void req_handler(ReqHandle *req_handle, void *_c) {
  auto *c = static_cast<AppContext *>(_c);
  if (config_num_bg_threads > 0) assert(c->rpc_->in_background());

  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  const auto *app_hdr = reinterpret_cast<app_hdr_t *>(req_msgbuf->buf_);

  auto &resp = req_handle->dyn_resp_msgbuf_;
  resp = c->rpc_->alloc_msg_buffer_or_die(app_hdr->resp_size_);

  *reinterpret_cast<app_hdr_t *>(resp.buf_) = *app_hdr;  // Copy app req header
  memset(resp.buf_ + sizeof(app_hdr_t), app_hdr->byte_contents_,
         app_hdr->resp_size_ - sizeof(app_hdr_t));

  size_t user_alloc_tot = c->rpc_->get_stat_user_alloc_tot();
  test_printf(
      "Server: Received request. Req/resp length: %zu/%zu. "
      "Rpc memory used = %zu bytes (%.3f MB)\n",
      app_hdr->req_size_, app_hdr->resp_size_, user_alloc_tot,
      1.0 * user_alloc_tot / MB(1));

  c->rpc_->enqueue_response(req_handle, &req_handle->dyn_resp_msgbuf_);
}

/// The common continuation function for all subtests. This checks that the
/// request buffer is identical to the response buffer, and increments the
/// number of responses in the context.
void cont_func(void *_c, void *_tag) {
  auto *c = static_cast<AppContext *>(_c);
  auto tag = reinterpret_cast<size_t>(_tag);

  const MsgBuffer &req_msgbuf = c->req_msgbufs_[tag];
  const MsgBuffer &resp_msgbuf = c->resp_msgbufs_[tag];
  const auto *app_hdr = reinterpret_cast<app_hdr_t *>(req_msgbuf.buf_);

  test_printf("Client: Received response. Req/resp length %zu/%zu.\n",
              req_msgbuf.get_data_size(), resp_msgbuf.get_data_size());

  // Check the response's header and contents
  assert(memcmp(req_msgbuf.buf_, resp_msgbuf.buf_, sizeof(app_hdr_t)) == 0);

  assert(resp_msgbuf.get_data_size() == app_hdr->resp_size_);
  for (size_t i = sizeof(app_hdr_t); i < resp_msgbuf.get_data_size(); i++) {
    assert(resp_msgbuf.buf_[i] == app_hdr->byte_contents_);
  }

  assert(c->is_client_);
  c->num_rpc_resps_++;
}

/// The generic test function that issues \p config_rpcs_per_session Rpcs
/// on each of \p config_num_sessions sessions, for multiple iterations.
///
/// The second \p size_t argument exists only because the client thread function
/// template in client_tests.h requires it.
void generic_test_func(Nexus *nexus, size_t) {
  // Create the Rpc and connect the session
  AppContext c;
  client_connect_sessions(nexus, c, config_num_sessions, basic_sm_handler);

  Rpc<CTransport> *rpc = c.rpc_;
  int *session_num_arr = c.session_num_arr_;

  // Pre-create MsgBuffers so we can test reuse and resizing
  size_t tot_reqs_per_iter = config_num_sessions * config_rpcs_per_session;
  c.req_msgbufs_.resize(tot_reqs_per_iter);
  c.resp_msgbufs_.resize(tot_reqs_per_iter);
  for (size_t i = 0; i < tot_reqs_per_iter; i++) {
    c.req_msgbufs_[i] = rpc->alloc_msg_buffer_or_die(rpc->get_max_msg_size());
    c.resp_msgbufs_[i] = rpc->alloc_msg_buffer_or_die(rpc->get_max_msg_size());
  }

  // The main request-issuing loop
  for (size_t iter = 0; iter < config_num_iters; iter++) {
    c.num_rpc_resps_ = 0;

    test_printf("Client: Iteration %zu.\n", iter);
    size_t iter_req_i = 0;  // Request MsgBuffer index in this iteration

    for (size_t sess_i = 0; sess_i < config_num_sessions; sess_i++) {
      for (size_t w_i = 0; w_i < config_rpcs_per_session; w_i++) {
        assert(iter_req_i < tot_reqs_per_iter);
        MsgBuffer &cur_req_msgbuf = c.req_msgbufs_[iter_req_i];

        // Generate request metadata
        size_t min_msg_size = sizeof(app_hdr_t) + sizeof(size_t);
        size_t req_size = get_rand_msg_size(&c.fastrand_, rpc, min_msg_size);
        size_t resp_size = get_rand_msg_size(&c.fastrand_, rpc, min_msg_size);
        uint8_t byte_contents = c.fastrand_.next_u32() % UINT8_MAX;

        rpc->resize_msg_buffer(&cur_req_msgbuf, req_size);
        auto *app_hdr = reinterpret_cast<app_hdr_t *>(cur_req_msgbuf.buf_);
        *app_hdr = app_hdr_t(req_size, resp_size, byte_contents);

        memset(cur_req_msgbuf.buf_ + sizeof(app_hdr_t), byte_contents,
               req_size - sizeof(app_hdr_t));

        rpc->enqueue_request(session_num_arr[sess_i], kTestReqType,
                             &cur_req_msgbuf, &c.resp_msgbufs_[iter_req_i],
                             cont_func, reinterpret_cast<void *>(iter_req_i));

        iter_req_i++;
      }
    }

    wait_for_rpc_resps_or_timeout(c, tot_reqs_per_iter);
    assert(c.num_rpc_resps_ == tot_reqs_per_iter);
  }

  // Free the MsgBuffers
  for (auto &mb : c.req_msgbufs_) rpc->free_msg_buffer(mb);
  for (auto &mb : c.resp_msgbufs_) rpc->free_msg_buffer(mb);

  // Disconnect the sessions
  for (size_t sess_i = 0; sess_i < config_num_sessions; sess_i++) {
    rpc->destroy_session(session_num_arr[sess_i]);
  }

  rpc->run_event_loop(kTestEventLoopMs);

  // Free resources
  delete rpc;
  client_done = true;
}

void launch_helper() {
  auto &reg_info_vec =
      config_num_bg_threads == 0 ? reg_info_vec_fg : reg_info_vec_bg;
  launch_server_client_threads(config_num_sessions, config_num_bg_threads,
                               generic_test_func, reg_info_vec,
                               ConnectServers::kFalse, 0.0);
}

TEST(OneLargeRpc, Foreground) {
  config_num_iters = 1;
  config_num_sessions = 1;
  config_rpcs_per_session = 1;
  config_num_bg_threads = 0;
  launch_helper();
}

TEST(OneLargeRpc, Background) {
  config_num_iters = 1;
  config_num_sessions = 1;
  config_rpcs_per_session = 1;
  config_num_bg_threads = 1;
  launch_helper();
}

TEST(MultiLargeRpcOneSession, Foreground) {
  config_num_iters = 1;
  config_num_sessions = 1;
  config_rpcs_per_session = kSessionReqWindow;
  config_num_bg_threads = 0;
  launch_helper();
}

TEST(MultiLargeRpcOneSession, Background) {
  config_num_iters = 1;
  config_num_sessions = 1;
  config_rpcs_per_session = kSessionReqWindow;
  config_num_bg_threads = 1;
  launch_helper();
}

TEST(MultiLargeRpcMultiSession, Foreground) {
  assert(erpc::is_log_level_reasonable());
  config_num_iters = 2;
  config_num_sessions = 4;
  config_rpcs_per_session = kSessionReqWindow;
  config_num_bg_threads = 0;
  launch_helper();
}

TEST(MultiLargeRpcMultiSession, Background) {
  assert(erpc::is_log_level_reasonable());
  config_num_iters = 2;
  config_num_sessions = 4;
  config_rpcs_per_session = kSessionReqWindow;
  config_num_bg_threads = 1;
  launch_helper();
}

TEST(DISABLED_MemoryLeak, Foreground) {
  assert(erpc::is_log_level_reasonable());
  config_num_iters = 50;
  config_num_sessions = 4;
  config_rpcs_per_session = kSessionReqWindow;
  config_num_bg_threads = 0;
  launch_helper();
}

TEST(DISABLED_MemoryLeak, Background) {
  assert(erpc::is_log_level_reasonable());
  config_num_iters = 50;
  config_num_sessions = 4;
  config_rpcs_per_session = kSessionReqWindow;
  config_num_bg_threads = 1;
  launch_helper();
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#include "client_tests.h"

void req_handler(ReqHandle *, void *);  // Forward declaration

/// Request handler for foreground testing
auto reg_info_vec_fg = {
    ReqFuncRegInfo(kTestReqType, req_handler, ReqFuncType::kForeground)};

/// Request handler for background testing
auto reg_info_vec_bg = {
    ReqFuncRegInfo(kTestReqType, req_handler, ReqFuncType::kBackground)};

/// Per-thread application context
class AppContext : public BasicAppContext {};

/// Configuration for controlling the test
size_t config_num_sessions;      ///< Number of sessions created by client
size_t config_num_bg_threads;    ///< Number of background threads
size_t config_rpcs_per_session;  ///< Number of Rpcs per session per iteration
size_t config_msg_size;  ///< The size of the request and response messages

/// The common request handler for all subtests. Copies the request message to
/// the response.
void req_handler(ReqHandle *req_handle, void *_c) {
  auto *c = static_cast<AppContext *>(_c);
  assert(!c->is_client_);

  if (config_num_bg_threads > 0) {
    assert(c->rpc_->in_background());
  }

  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  size_t resp_size = req_msgbuf->get_data_size();
  Rpc<CTransport>::resize_msg_buffer(&req_handle->pre_resp_msgbuf_, resp_size);
  memcpy(req_handle->pre_resp_msgbuf_.buf_, req_msgbuf->buf_, resp_size);
  c->rpc_->enqueue_response(req_handle, &req_handle->pre_resp_msgbuf_);
}

/// The common continuation function for all subtests. This checks that the
/// request buffer is identical to the response buffer, and increments the
/// number of responses in the context
void cont_func(void *_c, void *_tag) {
  auto *c = static_cast<AppContext *>(_c);
  size_t tag = reinterpret_cast<size_t>(_tag);
  ASSERT_EQ(c->resp_msgbufs_[tag].get_data_size(), config_msg_size);

  for (size_t i = 0; i < config_msg_size; i++) {
    ASSERT_EQ(c->resp_msgbufs_[tag].buf_[i], static_cast<uint8_t>(tag));
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
    const size_t max_data_pkt = rpc->get_max_data_per_pkt();
    c.req_msgbufs_[i] = rpc->alloc_msg_buffer_or_die(max_data_pkt);
    c.resp_msgbufs_[i] = rpc->alloc_msg_buffer_or_die(max_data_pkt);
  }

  // The main request-issuing loop
  for (size_t iter = 0; iter < 2; iter++) {
    c.num_rpc_resps_ = 0;

    test_printf("Client: Iteration %zu.\n", iter);
    size_t iter_req_i = 0;  // Request MsgBuffer index in an iteration

    for (size_t sess_i = 0; sess_i < config_num_sessions; sess_i++) {
      for (size_t w_i = 0; w_i < config_rpcs_per_session; w_i++) {
        assert(iter_req_i < tot_reqs_per_iter);
        MsgBuffer &cur_req_msgbuf = c.req_msgbufs_[iter_req_i];

        rpc->resize_msg_buffer(&cur_req_msgbuf, config_msg_size);
        for (size_t i = 0; i < config_msg_size; i++) {
          cur_req_msgbuf.buf_[i] = static_cast<uint8_t>(iter_req_i);
        }

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

TEST(OneSmallRpc, Foreground) {
  config_num_sessions = 1;
  config_num_bg_threads = 0;
  config_rpcs_per_session = 1;
  config_msg_size = Rpc<CTransport>::get_max_data_per_pkt();
  launch_helper();
}

TEST(OneSmallRpc, Background) {
  config_num_sessions = 1;
  config_num_bg_threads = 1;
  config_rpcs_per_session = 1;
  config_msg_size = Rpc<CTransport>::get_max_data_per_pkt();
  launch_helper();
}

TEST(MultiSmallRpcOneSession, Foreground) {
  config_num_sessions = 1;
  config_num_bg_threads = 0;
  config_rpcs_per_session = kSessionReqWindow;
  config_msg_size = Rpc<CTransport>::get_max_data_per_pkt();
  launch_helper();
}

TEST(MultiSmallRpcOneSession, Background) {
  config_num_sessions = 1;
  config_num_bg_threads = 2;
  config_rpcs_per_session = kSessionReqWindow;
  config_msg_size = Rpc<CTransport>::get_max_data_per_pkt();
  launch_helper();
}

TEST(MultiSmallRpcMultiSession, Foreground) {
  config_num_sessions = 4;
  config_num_bg_threads = 0;
  config_rpcs_per_session = kSessionReqWindow;
  config_msg_size = Rpc<CTransport>::get_max_data_per_pkt();
  launch_helper();
}

TEST(MultiSmallRpcMultiSession, Background) {
  config_num_sessions = 4;
  config_num_bg_threads = 3;
  config_rpcs_per_session = kSessionReqWindow;
  config_msg_size = Rpc<CTransport>::get_max_data_per_pkt();
  launch_helper();
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

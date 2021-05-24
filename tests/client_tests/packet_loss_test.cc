/**
 * @file packet_loss_test.cc
 * @brief Test packet loss handling
 */
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

static constexpr double kPktDropProb = 0.3;

/// Configuration for controlling the test
size_t config_num_iters;       ///< The number of iterations
size_t config_num_rpcs;        ///< Number of Rpcs per iteration
size_t config_num_bg_threads;  ///< Number of background threads

/// The common request handler for all subtests
void req_handler(ReqHandle *req_handle, void *_c) {
  auto *c = static_cast<AppContext *>(_c);
  if (config_num_bg_threads > 0) assert(c->rpc_->in_background());

  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  size_t resp_size = req_msgbuf->get_data_size();

  req_handle->dyn_resp_msgbuf_ = c->rpc_->alloc_msg_buffer_or_die(resp_size);
  size_t user_alloc_tot = c->rpc_->get_stat_user_alloc_tot();

  memcpy(reinterpret_cast<char *>(req_handle->dyn_resp_msgbuf_.buf_),
         reinterpret_cast<char *>(req_msgbuf->buf_), resp_size);

  test_printf(
      "Server: Received request of length %zu. "
      "Rpc memory used = %zu bytes (%.3f MB)\n",
      resp_size, user_alloc_tot, 1.0 * user_alloc_tot / MB(1));

  c->rpc_->enqueue_response(req_handle, &req_handle->dyn_resp_msgbuf_);
}

/// The common continuation function for all subtests
void cont_func(void *_c, void *_tag) {
  auto *c = static_cast<AppContext *>(_c);
  auto tag = reinterpret_cast<size_t>(_tag);
  const MsgBuffer &resp_msgbuf = c->resp_msgbufs_[tag];
  test_printf("Client: Received response of length %zu.\n",
              resp_msgbuf.get_data_size());

  for (size_t i = 0; i < resp_msgbuf.get_data_size(); i++) {
    ASSERT_EQ(resp_msgbuf.buf_[i], static_cast<uint8_t>(tag));
  }

  assert(c->is_client_);
  c->num_rpc_resps_++;
}

/// The generic test function that issues \p config_num_rpcs Rpcs
/// on the session, for multiple iterations.
///
/// The second \p size_t argument exists only because the client thread function
/// template in client_tests.h requires it.
void generic_test_func(Nexus *nexus, size_t) {
  // Create the Rpc and connect the session
  AppContext c;
  client_connect_sessions(nexus, c, 1, basic_sm_handler);  // 1 session

  Rpc<CTransport> *rpc = c.rpc_;
  rpc->fault_inject_set_pkt_drop_prob_st(kPktDropProb);

  // Pre-create MsgBuffers so we can test reuse and resizing
  c.req_msgbufs_.resize(config_num_rpcs);
  c.resp_msgbufs_.resize(config_num_rpcs);
  for (size_t i = 0; i < config_num_rpcs; i++) {
    c.req_msgbufs_[i] = rpc->alloc_msg_buffer_or_die(rpc->get_max_msg_size());
    c.resp_msgbufs_[i] = rpc->alloc_msg_buffer_or_die(rpc->get_max_msg_size());
  }

  // The main request-issuing loop
  for (size_t iter = 0; iter < config_num_iters; iter++) {
    c.num_rpc_resps_ = 0;

    test_printf("Client: Iteration %zu.\n", iter);
    size_t iter_req_i = 0;  // Request MsgBuffer index in this iteration

    for (size_t w_i = 0; w_i < config_num_rpcs; w_i++) {
      assert(iter_req_i < config_num_rpcs);
      MsgBuffer &cur_req_msgbuf = c.req_msgbufs_[iter_req_i];

      // Don't use very large requests because we drop a lot of packets
      size_t req_pkts =
          (kSessionCredits * 2) + (c.fastrand_.next_u32() % kSessionCredits);
      size_t req_size = req_pkts * rpc->get_max_data_per_pkt();

      rpc->resize_msg_buffer(&cur_req_msgbuf, req_size);
      memset(cur_req_msgbuf.buf_, iter_req_i, req_size);

      rpc->enqueue_request(c.session_num_arr_[0], kTestReqType, &cur_req_msgbuf,
                           &c.resp_msgbufs_[iter_req_i], cont_func,
                           reinterpret_cast<void *>(iter_req_i));

      iter_req_i++;
    }

    // The default timeout for tests is 20 seconds. This test takes more time
    // because of packet drops.
    for (size_t i = 0; i < 5; i++) {
      wait_for_rpc_resps_or_timeout(c, config_num_rpcs);
      if (c.num_rpc_resps_ == config_num_rpcs) break;
    }
    assert(c.num_rpc_resps_ == config_num_rpcs);
  }

  // Free the MsgBuffers
  for (auto &mb : c.req_msgbufs_) rpc->free_msg_buffer(mb);
  for (auto &mb : c.resp_msgbufs_) rpc->free_msg_buffer(mb);

  rpc->destroy_session(c.session_num_arr_[0]);
  rpc->run_event_loop(kTestEventLoopMs);

  // Free resources
  delete rpc;
  client_done = true;
}

void launch_helper() {
  auto &reg_info_vec =
      config_num_bg_threads == 0 ? reg_info_vec_fg : reg_info_vec_bg;
  launch_server_client_threads(1 /* num_sessions */, config_num_bg_threads,
                               generic_test_func, reg_info_vec,
                               ConnectServers::kFalse, kPktDropProb);
}

TEST(OneLargeRpc, Foreground) {
  config_num_iters = 1;
  config_num_rpcs = 1;
  config_num_bg_threads = 0;
  launch_helper();
}

TEST(OneLargeRpc, Background) {
  config_num_iters = 1;
  config_num_rpcs = 1;
  config_num_bg_threads = 1;
  launch_helper();
}

TEST(MultiLargeRpcOneSession, Foreground) {
  config_num_iters = 2;
  config_num_rpcs = kSessionReqWindow;
  config_num_bg_threads = 0;
  launch_helper();
}

TEST(MultiLargeRpcOneSession, Background) {
  config_num_iters = 2;
  config_num_rpcs = kSessionReqWindow;
  config_num_bg_threads = 1;
  launch_helper();
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

/**
 * @file server_failure_test.cc
 * @brief Test server failure handling at client
 */

// private <- public breaks gtest, so include it first. This overrides the
// gtest.h include in client_tests.h
#include <gtest/gtest.h>
#define private public

#include "client_tests.h"

void req_handler(ReqHandle *, void *);  // Forward declaration

/// Per-thread application context
class AppContext : public BasicAppContext {
 public:
  FastRand fastrand;  ///< Used for picking large message sizes
};

/// Configuration for controlling the test
size_t config_num_rpcs;  ///< Number of Rpcs per iteration

/// The common request handler for all subtests
void req_handler(ReqHandle *req_handle, void *_c) {
  auto *c = static_cast<AppContext *>(_c);

  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  size_t resp_size = req_msgbuf->get_data_size();

  req_handle->dyn_resp_msgbuf = c->rpc->alloc_msg_buffer_or_die(resp_size);
  size_t user_alloc_tot = c->rpc->get_stat_user_alloc_tot();

  memcpy(reinterpret_cast<char *>(req_handle->dyn_resp_msgbuf.buf),
         reinterpret_cast<char *>(req_msgbuf->buf), resp_size);

  test_printf(
      "Server: Received request of length %zu. "
      "Rpc memory used = %zu bytes (%.3f MB)\n",
      resp_size, user_alloc_tot, 1.0 * user_alloc_tot / MB(1));

  c->rpc->enqueue_response(req_handle, &req_handle->dyn_resp_msgbuf);
}

/// The common continuation function for all subtests
void cont_func(void *_c, void *_tag) {
  auto *c = static_cast<AppContext *>(_c);
  size_t tag = reinterpret_cast<size_t>(_tag);
  const MsgBuffer &resp_msgbuf = c->resp_msgbufs[tag];

  assert(resp_msgbuf.get_data_size() == 0);
  c->num_rpc_resps++;
}

/// The generic client test function
///
/// The second \p size_t argument exists only because the client thread function
/// template in client_tests.h requires it.
void generic_test_func(Nexus *nexus, size_t) {
  // Create the Rpc and connect the session
  AppContext c;
  client_connect_sessions(nexus, c, 1, basic_sm_handler);  // 1 session

  Rpc<CTransport> *rpc = c.rpc;

  c.req_msgbufs.resize(config_num_rpcs);
  c.resp_msgbufs.resize(config_num_rpcs);
  for (size_t i = 0; i < config_num_rpcs; i++) {
    c.req_msgbufs[i] = rpc->alloc_msg_buffer_or_die(rpc->get_max_msg_size());
    c.resp_msgbufs[i] = rpc->alloc_msg_buffer_or_die(rpc->get_max_msg_size());
  }

  // Issue requests
  for (size_t i = 0; i < config_num_rpcs; i++) {
    c.num_rpc_resps = 0;
    const size_t req_size = c.fastrand.next_u32() % rpc->get_max_msg_size();
    rpc->resize_msg_buffer(&c.req_msgbufs[i], req_size);

    rpc->enqueue_request(c.session_num_arr[0], kTestReqType, &c.req_msgbufs[i],
                         &c.resp_msgbufs[i], cont_func,
                         reinterpret_cast<void *>(i) /* tag */);
  }

  auto session_num = static_cast<size_t>(c.session_num_arr[0]);
  for (auto &s : c.rpc->session_vec[session_num]->sslot_arr) {
    _unused(s);
    // XXX:
    // s.client_info.enqueue_request_tsc -=
    //    ms_to_cycles(rpc->kServerFailureTimeoutMs, rpc->get_freq_ghz());
  }

  rpc->run_event_loop_once();
  assert(c.num_rpc_resps == config_num_rpcs);

  // Free the MsgBuffers
  for (auto &mb : c.req_msgbufs) rpc->free_msg_buffer(mb);
  for (auto &mb : c.resp_msgbufs) rpc->free_msg_buffer(mb);

  rpc->destroy_session(c.session_num_arr[0]);
  rpc->run_event_loop(kTestEventLoopMs);

  // Free resources
  delete rpc;
  client_done = true;
}

void launch_helper() {
  auto reg_info_vec = {
      ReqFuncRegInfo(kTestReqType, req_handler, ReqFuncType::kForeground)};
  launch_server_client_threads(1 /* num_sessions */, 0 /* background threads */,
                               generic_test_func, reg_info_vec,
                               ConnectServers::kFalse, 0.0 /* pkt drop */);
}

TEST(Base, OneLargeRpc) {
  config_num_rpcs = 1;
  launch_helper();
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#include "protocol_tests.h"

namespace erpc {

class RpcSmTest : public RpcTest {
 public:
  RpcSmTest() {
    rpc_->udp_client_.enable_recording();  // Record UDP transmissions
  }

  /// A reusable check for session management tests. For the check to pass:
  /// 1. \p rpc must have \p num_sessions sessions in its session vector
  /// 2. \p rpc's UDP client must have a packet in its queue. The packet at the
  ///    front must match \p pkt_type and err_type.
  void common_check(size_t num_sessions, SmPktType pkt_type,
                    SmErrType err_type) const {
    ASSERT_EQ(rpc_->session_vec_.size(), num_sessions);
    ASSERT_FALSE(rpc_->udp_client_.sent_vec_.empty());
    const SmPkt &resp = rpc_->udp_client_.sent_vec_.back();
    ASSERT_EQ(resp.pkt_type_, pkt_type);
    ASSERT_EQ(resp.err_type_, err_type);
  }

 private:
};

//
// handle_connect_req_st()
//
TEST_F(RpcSmTest, handle_connect_req_st_reordering) {
  const auto client = get_remote_endpoint();
  const auto server = set_invalid_session_num(get_local_endpoint());

  const SmPkt conn_req(SmPktType::kConnectReq, SmErrType::kNoError,
                       kTestUniqToken, client, server);

  // Process first connect request - session is created
  rpc_->handle_connect_req_st(conn_req);
  common_check(1, SmPktType::kConnectResp, SmErrType::kNoError);

  // Process connect request again.
  // New session is not created and response is re-sent.
  rpc_->handle_connect_req_st(conn_req);
  common_check(1, SmPktType::kConnectResp, SmErrType::kNoError);

  // Destroy the session and re-handle connect request.
  // New session is not created and no response is sent.
  rpc_->bury_session_st(rpc_->session_vec_[0]);
  rpc_->udp_client_.sent_vec_.clear();
  rpc_->handle_connect_req_st(conn_req);
  ASSERT_TRUE(rpc_->udp_client_.sent_vec_.empty());

  // Delete the client's connect request token and re-handle connect request.
  // New session *is* created and response is re-sent.
  rpc_->conn_req_token_map_.clear();
  rpc_->session_vec_.clear();
  rpc_->handle_connect_req_st(conn_req);
  common_check(1, SmPktType::kConnectResp, SmErrType::kNoError);
}

TEST_F(RpcSmTest, handle_connect_req_st_errors) {
  const auto client = get_remote_endpoint();
  const auto server = set_invalid_session_num(get_local_endpoint());

  const SmPkt conn_req(SmPktType::kConnectReq, SmErrType::kNoError,
                       kTestUniqToken, client, server);

  // Transport type mismatch
  SmPkt ttm_conn_req = conn_req;
  ttm_conn_req.server_.transport_type_ = TransportType::kInvalid;
  rpc_->handle_connect_req_st(ttm_conn_req);
  common_check(0, SmPktType::kConnectResp, SmErrType::kInvalidTransport);

  // Ring entries exhausted
  const size_t initial_ring_entries_available = rpc_->ring_entries_available_;
  rpc_->ring_entries_available_ = kSessionCredits - 1;
  rpc_->handle_connect_req_st(conn_req);
  common_check(0, SmPktType::kConnectResp, SmErrType::kRingExhausted);
  rpc_->ring_entries_available_ = initial_ring_entries_available;  // Restore

  // Ring entries exhausted
  rpc_->ring_entries_available_ = 0;
  rpc_->handle_connect_req_st(conn_req);
  common_check(0, SmPktType::kConnectResp, SmErrType::kRingExhausted);
  rpc_->ring_entries_available_ = initial_ring_entries_available;

  // Client routing info resolution fails
  rpc_->fault_inject_fail_resolve_rinfo_st();
  rpc_->handle_connect_req_st(conn_req);
  common_check(0, SmPktType::kConnectResp,
               SmErrType::kRoutingResolutionFailure);
  rpc_->faults_.fail_resolve_rinfo_ = false;  // Restore

  // Out of hugepages
  //
  // This should be the last subtest because we use alloc_raw() to eat up
  // hugepages rapidly by avoiding registration. These hugepages cannot be freed
  // without deleting the allocator.
  //
  // We hoard hugepages in two steps. First in large chunks for speed, then
  // until MTU-sized pages cannot be allocated.
  while (true) {
    Buffer buffer = rpc_->huge_alloc_->alloc_raw(MB(16), DoRegister::kFalse);
    if (buffer.buf_ == nullptr) break;
  }

  while (true) {
    auto msgbuf = rpc_->alloc_msg_buffer(rpc_->get_max_data_per_pkt());
    if (msgbuf.buf_ == nullptr) break;
  }

  size_t initial_alloc = rpc_->huge_alloc_->get_stat_user_alloc_tot();
  rpc_->handle_connect_req_st(conn_req);
  common_check(0, SmPktType::kConnectResp, SmErrType::kOutOfMemory);
  ASSERT_EQ(initial_alloc, rpc_->huge_alloc_->get_stat_user_alloc_tot());
  // No more tests here because all hugepages are consumed
}

//
// handle_connect_resp_st()
//

TEST_F(RpcSmTest, handle_connect_resp_st_reordering) {
  const auto client = get_local_endpoint();
  const auto server = get_remote_endpoint();
  const SmPkt conn_resp(SmPktType::kConnectResp, SmErrType::kNoError,
                        kTestUniqToken, client, server);

  // Make session 0 a client session in kConnectInProgress
  create_client_session_init(client, server);

  // Process connect response. Session is connected, server session number saved
  rpc_->handle_connect_resp_st(conn_resp);
  ASSERT_EQ(rpc_->session_vec_[0]->state_, SessionState::kConnected);
  ASSERT_EQ(rpc_->session_vec_[0]->server_.session_num_, 1);

  // Process connect response again. This gets ignored.
  rpc_->handle_connect_resp_st(conn_resp);
  ASSERT_EQ(rpc_->session_vec_[0]->state_, SessionState::kConnected);
  ASSERT_EQ(rpc_->session_vec_.size(), 1);

  // Artificially destroy the session. Response gets ignored.
  Session *clt_session = rpc_->session_vec_[0];
  rpc_->session_vec_[0] = nullptr;
  rpc_->handle_connect_resp_st(conn_resp);  // No crash
  rpc_->session_vec_[0] = clt_session;      // Restore
}

TEST_F(RpcSmTest, handle_connect_resp_st_resolve_error) {
  const auto client = get_local_endpoint();
  const auto server = get_remote_endpoint();
  const SmPkt conn_resp(SmPktType::kConnectResp, SmErrType::kNoError,
                        kTestUniqToken, client, server);

  // Make session 0 a client session in kConnectInProgress
  create_client_session_init(client, server);

  // Fail server routing resolution. Disconnect request is sent but session
  // is not destroyed.
  rpc_->fault_inject_fail_resolve_rinfo_st();
  rpc_->handle_connect_resp_st(conn_resp);
  common_check(1, SmPktType::kDisconnectReq, SmErrType::kNoError);
  ASSERT_EQ(rpc_->session_vec_[0]->state_, SessionState::kDisconnectInProgress);
  ASSERT_NE(rpc_->session_vec_[0], nullptr);
}

TEST_F(RpcSmTest, handle_connect_resp_st_response_error) {
  const auto client = get_local_endpoint();
  const auto server = get_remote_endpoint();
  const SmPkt conn_resp(SmPktType::kConnectResp, SmErrType::kRingExhausted,
                        kTestUniqToken, client, server);

  // Make session 0 a client session in kConnectInProgress
  create_client_session_init(client, server);

  // Process response with error. Session is destroyed and ring buffers released
  rpc_->handle_connect_resp_st(conn_resp);
  ASSERT_EQ(rpc_->session_vec_[0], nullptr);
  ASSERT_TRUE(rpc_->ring_entries_available_ ==
              rpc_->transport_->kNumRxRingEntries);
  // No more tests here because session is destroyed
}

//
// handle_disconnect_req_st()
//
TEST_F(RpcSmTest, handle_disconnect_req_st) {
  const auto client = get_remote_endpoint();
  const auto server = get_local_endpoint();
  const SmPkt disc_req(SmPktType::kDisconnectReq, SmErrType::kNoError,
                       kTestUniqToken, client, server);

  // Make session 0 a server session in kConnected
  create_server_session_init(client, server);

  // Process first disconnect request
  // Session is destroyed, resources released, & response sent.
  const size_t initial_alloc = rpc_->huge_alloc_->get_stat_user_alloc_tot();
  rpc_->handle_disconnect_req_st(disc_req);
  common_check(1, SmPktType::kDisconnectResp, SmErrType::kNoError);
  ASSERT_EQ(rpc_->session_vec_[0], nullptr);
  ASSERT_LT(rpc_->huge_alloc_->get_stat_user_alloc_tot(), initial_alloc);
  ASSERT_TRUE(rpc_->ring_entries_available_ ==
              rpc_->transport_->kNumRxRingEntries);

  // Process disconnect request again. Response is re-sent.
  rpc_->handle_disconnect_req_st(disc_req);
  common_check(1, SmPktType::kDisconnectResp, SmErrType::kNoError);
}

//
// handle_disconnect_resp_st()
//
TEST_F(RpcSmTest, handle_disconnect_resp_st) {
  const auto client = get_local_endpoint();
  const auto server = get_remote_endpoint();
  const SmPkt disc_resp(SmPktType::kDisconnectResp, SmErrType::kNoError,
                        kTestUniqToken, client, server);

  // Make session 0 a client session in kDisconnectInProgress
  create_client_session_init(client, server);  // In kConnectInProgress for now
  rpc_->session_vec_[0]->state_ = SessionState::kDisconnectInProgress;
  rpc_->session_vec_[0]->server_.session_num_ = server.session_num_;

  // Process first disconnect response
  rpc_->handle_disconnect_resp_st(disc_resp);
  ASSERT_EQ(rpc_->session_vec_[0], nullptr);
  ASSERT_TRUE(rpc_->ring_entries_available_ ==
              rpc_->transport_->kNumRxRingEntries);

  // Process disconnect request again. This gets ignored.
  rpc_->handle_disconnect_resp_st(disc_resp);
}

//
// create_session_st()
//
TEST_F(RpcSmTest, create_session_st) {
  // Correct args
  int session_num = rpc_->create_session("127.0.0.1:31850", kTestRpcId + 1);
  ASSERT_EQ(session_num, 0);
  common_check(1, SmPktType::kConnectReq, SmErrType::kNoError);
  ASSERT_EQ(rpc_->session_vec_[0]->state_, SessionState::kConnectInProgress);

  // Try to create session to self
  session_num = rpc_->create_session("127.0.0.1:31850", kTestRpcId);
  ASSERT_LT(session_num, 0);
}

}  // namespace erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

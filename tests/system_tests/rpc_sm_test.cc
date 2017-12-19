#include "system_tests.h"

namespace erpc {

void req_handler(ReqHandle *, void *) {}  // Required by RpcTest

class RpcSmTest : public RpcTest {
 public:
  RpcSmTest() {
    rpc->udp_client.enable_recording();  // Record UDP transmissions
  }

  /// A reusable check for session management tests. For the check to pass:
  /// 1. \p rpc must have \p num_sessions sessions in its session vector
  /// 2. \p rpc's UDP client must have a packet in its queue. The packet at the
  ///    front must match \p pkt_type and err_type.
  void common_check(size_t num_sessions, SmPktType pkt_type,
                    SmErrType err_type) const {
    ASSERT_EQ(rpc->session_vec.size(), num_sessions);
    ASSERT_FALSE(rpc->udp_client.sent_vec.empty());
    const SmPkt &resp = rpc->udp_client.sent_vec.back();
    ASSERT_EQ(resp.pkt_type, pkt_type);
    ASSERT_EQ(resp.err_type, err_type);
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
  rpc->handle_connect_req_st(conn_req);
  common_check(1, SmPktType::kConnectResp, SmErrType::kNoError);

  // Process connect request again.
  // New session is not created and response is re-sent.
  rpc->handle_connect_req_st(conn_req);
  common_check(1, SmPktType::kConnectResp, SmErrType::kNoError);

  // Destroy the session and re-handle connect request.
  // New session is not created and no response is sent.
  rpc->bury_session_st(rpc->session_vec[0]);
  rpc->udp_client.sent_vec.clear();
  rpc->handle_connect_req_st(conn_req);
  ASSERT_TRUE(rpc->udp_client.sent_vec.empty());

  // Delete the client's connect request token and re-handle connect request.
  // New session *is* created and response is re-sent.
  rpc->conn_req_token_map.clear();
  rpc->session_vec.clear();
  rpc->handle_connect_req_st(conn_req);
  common_check(1, SmPktType::kConnectResp, SmErrType::kNoError);
}

TEST_F(RpcSmTest, handle_connect_req_st_errors) {
  const auto client = get_remote_endpoint();
  const auto server = set_invalid_session_num(get_local_endpoint());

  const SmPkt conn_req(SmPktType::kConnectReq, SmErrType::kNoError,
                       kTestUniqToken, client, server);

  // Transport type mismatch
  SmPkt ttm_conn_req = conn_req;
  ttm_conn_req.server.transport_type = Transport::TransportType::kInvalid;
  rpc->handle_connect_req_st(ttm_conn_req);
  common_check(0, SmPktType::kConnectResp, SmErrType::kInvalidTransport);

  // Transport type mismatch
  SmPkt pm_conn_req = conn_req;
  pm_conn_req.server.phy_port = kInvalidPhyPort;
  rpc->handle_connect_req_st(pm_conn_req);
  common_check(0, SmPktType::kConnectResp, SmErrType::kInvalidRemotePort);

  // Ring entries exhausted
  const size_t initial_ring_entries_available = rpc->ring_entries_available;
  rpc->ring_entries_available = Session::kSessionCredits - 1;
  rpc->handle_connect_req_st(conn_req);
  common_check(0, SmPktType::kConnectResp, SmErrType::kRingExhausted);
  rpc->ring_entries_available = initial_ring_entries_available;  // Restore

  // Too many sessions
  rpc->session_vec.resize(kMaxSessionsPerThread, nullptr);
  rpc->handle_connect_req_st(conn_req);
  common_check(kMaxSessionsPerThread, SmPktType::kConnectResp,
               SmErrType::kTooManySessions);
  rpc->session_vec.clear();  // Restore

  // Client routing info resolution fails
  rpc->fault_inject_fail_resolve_rinfo_st();
  rpc->handle_connect_req_st(conn_req);
  common_check(0, SmPktType::kConnectResp,
               SmErrType::kRoutingResolutionFailure);
  rpc->faults.fail_resolve_rinfo = false;  // Restore

  // Out of hugepages
  //
  // This should be the last subtest because we use alloc_raw() to eat up
  // hugepages rapidly by avoiding registration. These hugepages cannot be freed
  // without deleting the allocator.
  //
  // We hoard hugepages in two steps. First in large chunks for speed, then
  // until MTU-sized pages cannot be allocated.
  while (true) {
    auto *buf = rpc->huge_alloc->alloc_raw(MB(16), kTestNumaNode, false);
    if (buf == nullptr) break;
  }

  while (true) {
    auto msgbuf = rpc->alloc_msg_buffer(rpc->get_max_data_per_pkt());
    if (msgbuf.buf == nullptr) break;
  }

  size_t initial_alloc = rpc->huge_alloc->get_stat_user_alloc_tot();
  rpc->handle_connect_req_st(conn_req);
  common_check(0, SmPktType::kConnectResp, SmErrType::kOutOfMemory);
  ASSERT_EQ(initial_alloc, rpc->huge_alloc->get_stat_user_alloc_tot());
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
  rpc->handle_connect_resp_st(conn_resp);
  ASSERT_EQ(rpc->session_vec[0]->state, SessionState::kConnected);
  ASSERT_EQ(rpc->session_vec[0]->server.session_num, 1);

  // Process connect response again. This gets ignored.
  rpc->handle_connect_resp_st(conn_resp);
  ASSERT_EQ(rpc->session_vec[0]->state, SessionState::kConnected);
  ASSERT_EQ(rpc->session_vec.size(), 1);

  // Artificially destroy the session. Response gets ignored.
  Session *clt_session = rpc->session_vec[0];
  rpc->session_vec[0] = nullptr;
  rpc->handle_connect_resp_st(conn_resp);  // No crash
  rpc->session_vec[0] = clt_session;       // Restore
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
  rpc->fault_inject_fail_resolve_rinfo_st();
  rpc->handle_connect_resp_st(conn_resp);
  common_check(1, SmPktType::kDisconnectReq, SmErrType::kNoError);
  ASSERT_EQ(rpc->session_vec[0]->state, SessionState::kDisconnectInProgress);
  ASSERT_NE(rpc->session_vec[0], nullptr);
}

TEST_F(RpcSmTest, handle_connect_resp_st_response_error) {
  const auto client = get_local_endpoint();
  const auto server = get_remote_endpoint();
  const SmPkt conn_resp(SmPktType::kConnectResp, SmErrType::kTooManySessions,
                        kTestUniqToken, client, server);

  // Make session 0 a client session in kConnectInProgress
  create_client_session_init(client, server);

  // Process response with error. Session is destroyed and ring buffers released
  rpc->handle_connect_resp_st(conn_resp);
  ASSERT_EQ(rpc->session_vec[0], nullptr);
  ASSERT_EQ(rpc->ring_entries_available, rpc->get_num_rx_ring_entries());
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
  const size_t initial_alloc = rpc->huge_alloc->get_stat_user_alloc_tot();
  rpc->handle_disconnect_req_st(disc_req);
  common_check(1, SmPktType::kDisconnectResp, SmErrType::kNoError);
  ASSERT_EQ(rpc->session_vec[0], nullptr);
  ASSERT_LT(rpc->huge_alloc->get_stat_user_alloc_tot(), initial_alloc);
  ASSERT_EQ(rpc->ring_entries_available, rpc->get_num_rx_ring_entries());

  // Process disconnect request again. Response is re-sent.
  rpc->handle_disconnect_req_st(disc_req);
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
  rpc->session_vec[0]->state = SessionState::kDisconnectInProgress;
  rpc->session_vec[0]->server.session_num = server.session_num;

  // Process first disconnect response
  rpc->handle_disconnect_resp_st(disc_resp);
  ASSERT_EQ(rpc->session_vec[0], nullptr);
  ASSERT_EQ(rpc->ring_entries_available, rpc->get_num_rx_ring_entries());

  // Process disconnect request again. This gets ignored.
  rpc->handle_disconnect_resp_st(disc_resp);
}

//
// create_session_st()
//
TEST_F(RpcSmTest, create_session_st) {
  // Correct args
  int session_num =
      rpc->create_session("localhost", kTestRpcId + 1, kTestPhyPort);
  ASSERT_EQ(session_num, 0);
  common_check(1, SmPktType::kConnectReq, SmErrType::kNoError);
  ASSERT_EQ(rpc->session_vec[0]->state, SessionState::kConnectInProgress);

  // Invalid remote port, which can be detected locally
  session_num = rpc->create_session("localhost", kTestRpcId + 1, kMaxPhyPorts);
  ASSERT_LT(session_num, 0);

  // Try to create session to self
  session_num = rpc->create_session("localhost", kTestRpcId, kTestPhyPort);
  ASSERT_LT(session_num, 0);
}

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

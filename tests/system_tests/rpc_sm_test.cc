#include <gtest/gtest.h>

#define private public
#include "rpc.h"

// These tests never run event loop, so SM pkts sent by Rpc have no consequence
namespace erpc {

static constexpr size_t kTestUdpPort = 3185;
static constexpr size_t kTestPhyPort = 0;
static constexpr size_t kTestNumaNode = 0;
static constexpr size_t kTestUniqToken = 42;
static constexpr size_t kTestBaseRpcId = 0;

typedef IBTransport TestTransport;

// An Rpc with no established sessions
class RpcSmTest : public ::testing::Test {
 public:
  static void sm_handler(int, SmEventType, SmErrType, void *) {}

  RpcSmTest() {
    nexus = new Nexus("localhost", kTestUdpPort);
    rt_assert(nexus != nullptr, "Failed to create nexus");
    nexus->drop_all_rx();  // Prevent SM thread from doing any work

    rpc = new Rpc<TestTransport>(nexus, nullptr, kTestBaseRpcId, sm_handler,
                                 kTestPhyPort, kTestNumaNode);
    rt_assert(rpc != nullptr, "Failed to create Rpc");

    rpc->udp_client.enable_recording();
  }

  ~RpcSmTest() {
    delete rpc;
    delete nexus;
  }

  SessionEndpoint gen_session_endpt(uint8_t rpc_id, uint16_t session_num) {
    rt_assert(rpc != nullptr, "gen_session_endpt() requires valid Rpc");

    SessionEndpoint se;
    se.transport_type = rpc->transport->transport_type;
    strcpy(se.hostname, "localhost");
    se.phy_port = kTestPhyPort;
    se.rpc_id = rpc_id;
    se.session_num = session_num;

    // Any routing info that's resolvable is fine
    rpc->transport->fill_local_routing_info(&se.routing_info);
    return se;
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

  /// Create a client session in its initial state
  static Session *create_client_session_init(const SessionEndpoint client,
                                             const SessionEndpoint server) {
    auto *clt_session = new Session(Session::Role::kClient, kTestUniqToken);
    clt_session->state = SessionState::kConnectInProgress;
    clt_session->client = client;
    clt_session->server = server;
    clt_session->server.session_num = kInvalidSessionNum;
    return clt_session;
  }

  Nexus *nexus = nullptr;
  Rpc<TestTransport> *rpc = nullptr;
};

//
// handle_connect_req_st()
//

TEST_F(RpcSmTest, handle_connect_req_st_reordering) {
  const auto client = gen_session_endpt(kTestBaseRpcId + 1, 0);
  const auto server = gen_session_endpt(kTestBaseRpcId, kInvalidSessionNum);
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

  // Delete the client's token and re-handle connect request.
  // New session *is* created and response is re-sent.
  rpc->sm_token_map.clear();
  rpc->session_vec.clear();
  rpc->handle_connect_req_st(conn_req);
  common_check(1, SmPktType::kConnectResp, SmErrType::kNoError);
}

TEST_F(RpcSmTest, handle_connect_req_st_errors) {
  const auto client = gen_session_endpt(kTestBaseRpcId + 1, 0);
  const auto server = gen_session_endpt(kTestBaseRpcId, kInvalidSessionNum);
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

  // RECVs exhausted
  const size_t initial_recvs_available = rpc->recvs_available;
  rpc->recvs_available = Session::kSessionCredits - 1;
  rpc->handle_connect_req_st(conn_req);
  common_check(0, SmPktType::kConnectResp, SmErrType::kRecvsExhausted);
  rpc->recvs_available = initial_recvs_available;  // Restore

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
  const auto client = gen_session_endpt(kTestBaseRpcId, 0);
  const auto server = gen_session_endpt(kTestBaseRpcId + 1, 1);
  const SmPkt conn_resp(SmPktType::kConnectResp, SmErrType::kNoError,
                        kTestUniqToken, client, server);

  // Make session 0 a client session in init state
  rpc->session_vec.push_back(create_client_session_init(client, server));

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
  const auto client = gen_session_endpt(kTestBaseRpcId, 0);
  const auto server = gen_session_endpt(kTestBaseRpcId + 1, 1);
  const SmPkt conn_resp(SmPktType::kConnectResp, SmErrType::kNoError,
                        kTestUniqToken, client, server);

  // Make session 0 a client session in init state
  rpc->session_vec.push_back(create_client_session_init(client, server));

  // Fail server routing resolution. Disconnect request is sent but session
  // is not destroyed.
  rpc->fault_inject_fail_resolve_rinfo_st();
  rpc->handle_connect_resp_st(conn_resp);
  common_check(1, SmPktType::kDisconnectReq, SmErrType::kNoError);
  ASSERT_EQ(rpc->session_vec[0]->state, SessionState::kDisconnectInProgress);
  ASSERT_NE(rpc->session_vec[0], nullptr);
}

TEST_F(RpcSmTest, handle_connect_resp_st_response_error) {
  const auto client = gen_session_endpt(kTestBaseRpcId, 0);
  const auto server = gen_session_endpt(kTestBaseRpcId + 1, 1);
  const SmPkt conn_resp(SmPktType::kConnectResp, SmErrType::kTooManySessions,
                        kTestUniqToken, client, server);

  // Make session 0 a client session in init state
  rpc->session_vec.push_back(create_client_session_init(client, server));

  // Process response with error. Session gets destroyed and RECVs are released.
  rpc->recvs_available -= Session::kSessionCredits;
  rpc->handle_connect_resp_st(conn_resp);
  ASSERT_EQ(rpc->session_vec[0], nullptr);
  ASSERT_EQ(rpc->recvs_available, rpc->transport->kRecvQueueDepth);
  // No more tests here because session is destroyed
}

//
// handle_disconnect_req_st()
//
/*
TEST_F(RpcSmTest, handle_disconnect_req_st_reordering) {
  const auto client = gen_session_endpt(kTestBaseRpcId + 1, 1);
  const auto server = gen_session_endpt(kTestBaseRpcId, 0);
  const SmPkt conn_req(SmPktType::kDisconnectReq, SmErrType::kNoError,
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

  // Delete the client's token and re-handle connect request.
  // New session *is* created and response is re-sent.
  rpc->sm_token_map.clear();
  rpc->session_vec.clear();
  rpc->handle_connect_req_st(conn_req);
  common_check(1, SmPktType::kConnectResp, SmErrType::kNoError);
}
*/

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#include <gtest/gtest.h>

#define private public
#include "rpc.h"

// These tests never run event loop, so SM pkts sent by Rpc have no consequence
namespace erpc {

typedef IBTransport TestTransport;

// An Rpc with no established sessions
class RpcTest : public ::testing::Test {
 public:
  static constexpr size_t kTestUdpPort = 3185;
  static constexpr size_t kTestPhyPort = 0;
  static constexpr size_t kTestRpcId = 0;
  static constexpr size_t kTestNumBgThreads = 0;
  static constexpr size_t kTestNumaNode = 0;
  static constexpr size_t kTestUniqToken = 42;

  static void sm_handler(int, SmEventType, SmErrType, void *) {}

  RpcTest() {
    nexus = new Nexus("localhost", kTestUdpPort, kTestNumBgThreads);
    rt_assert(nexus != nullptr, "RpcTest: Failed to create nexus");
    nexus->drop_all_rx();

    rpc = new Rpc<TestTransport>(nexus, nullptr, kTestRpcId, sm_handler,
                                 kTestPhyPort, kTestNumaNode);
    rt_assert(rpc != nullptr, "RpcTest: Failed to create Rpc");

    rpc->udp_client.enable_recording();

    // int sn = rpc->create_session("localhost", kServerRpcId, kPhyPort);
    // rt_assert(sn == 0, "RpcTest: Failed to create session");
  }

  ~RpcTest() {
    delete rpc;
    delete nexus;
  }

  SessionEndpoint gen_session_endpoint(uint8_t rpc_id, uint16_t session_num) {
    rt_assert(rpc != nullptr,
              "RpcTest: gen_session_endpoint() requires valid Rpc");

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

  Nexus *nexus = nullptr;
  Rpc<TestTransport> *rpc = nullptr;
};

/// A reusable check used by session management tests. For the check to pass:
/// 1. \p rpc must have \p num_sessions sessions in its session vector
/// 2. \p rpc's UDP client must have a packet in its queue. The packet at the
///    front must match \p pkt_type and err_type.
void test_sm_check(Rpc<TestTransport> *rpc, size_t num_sessions,
                   SmPktType pkt_type, SmErrType err_type) {
  ASSERT_EQ(rpc->session_vec.size(), num_sessions);
  const SmPkt resp = rpc->udp_client.sent_queue_pop();
  ASSERT_EQ(resp.pkt_type, pkt_type);
  ASSERT_EQ(resp.err_type, err_type);
}

/// Test SM packet reordering for handle_connect_req_st()
TEST_F(RpcTest, handle_connect_req_st_reordering) {
  const auto server = gen_session_endpoint(kTestRpcId, kInvalidSessionNum);
  const auto client = gen_session_endpoint(kTestRpcId + 1, /* session num */ 0);
  const SmPkt conn_req(SmPktType::kConnectReq, SmErrType::kNoError,
                       kTestUniqToken, client, server);
  SmPkt resp;

  // Process first connect request - session is created
  rpc->handle_connect_req_st(conn_req);
  test_sm_check(rpc, 1, SmPktType::kConnectResp, SmErrType::kNoError);

  // Process connect request again.
  // New session is not created and response is re-sent.
  rpc->handle_connect_req_st(conn_req);
  test_sm_check(rpc, 1, SmPktType::kConnectResp, SmErrType::kNoError);

  // Destroy the session and re-handle connect request.
  // New session is not created and response is not sent.
  rpc->bury_session_st(rpc->session_vec[0]);
  rpc->handle_connect_req_st(conn_req);
  ASSERT_EQ(rpc->udp_client.sent_queue.empty(), true);

  // Delete the client's token and re-handle connect request.
  // New session *is* created and response is re-sent.
  rpc->sm_token_map.clear();
  rpc->session_vec.clear();
  rpc->handle_connect_req_st(conn_req);
  test_sm_check(rpc, 1, SmPktType::kConnectResp, SmErrType::kNoError);
}

/// Test error cases for handle_connect_req_st()
TEST_F(RpcTest, handle_connect_req_st_errors) {
  const auto server = gen_session_endpoint(kTestRpcId, kInvalidSessionNum);
  const auto client = gen_session_endpoint(kTestRpcId + 1, /* session num */ 0);
  const SmPkt conn_req(SmPktType::kConnectReq, SmErrType::kNoError,
                       kTestUniqToken, client, server);
  SmPkt resp;

  // Transport type mismatch. Session is not created & resp with error is sent.
  SmPkt ttm_conn_req = conn_req;
  ttm_conn_req.server.transport_type = Transport::TransportType::kInvalid;
  rpc->handle_connect_req_st(ttm_conn_req);
  test_sm_check(rpc, 0, SmPktType::kConnectResp, SmErrType::kInvalidTransport);

  // Transport type mismatch. Session is not created & resp with error is sent.
  SmPkt pm_conn_req = conn_req;
  pm_conn_req.server.phy_port = kInvalidPhyPort;
  rpc->handle_connect_req_st(pm_conn_req);
  test_sm_check(rpc, 0, SmPktType::kConnectResp, SmErrType::kInvalidRemotePort);

  // Recvs exhausted. Session is not created and resp with error is sent.
  const size_t initial_recvs_available = rpc->recvs_available;
  rpc->recvs_available = Session::kSessionCredits - 1;
  rpc->handle_connect_req_st(conn_req);
  test_sm_check(rpc, 0, SmPktType::kConnectResp, SmErrType::kRecvsExhausted);
  rpc->recvs_available = initial_recvs_available;  // Restore

  // Too many sessions. Session is not created and resp with error is sent
  rpc->session_vec.resize(kMaxSessionsPerThread, nullptr);
  rpc->handle_connect_req_st(conn_req);
  test_sm_check(rpc, kMaxSessionsPerThread, SmPktType::kConnectResp,
                SmErrType::kTooManySessions);
  rpc->session_vec.clear();  // Restore
}

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

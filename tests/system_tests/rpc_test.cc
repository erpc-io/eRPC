#include <gtest/gtest.h>

#define private public
#include "rpc.h"

// These tests never run event loop, so SM pkts sent by Rpc have no consequence
namespace erpc {

// An Rpc with no established sessions
class RpcTest : public ::testing::Test {
 public:
  static constexpr size_t kUdpPort = 3185;
  static constexpr size_t kPhyPort = 0;
  static constexpr size_t kRpcId = 0;
  static constexpr size_t kNumBgThreads = 0;
  static constexpr size_t kNumaNode = 0;
  static constexpr size_t kUniqToken = 42;
  static constexpr auto transport_type = Transport::TransportType::kInfiniBand;

  static void sm_handler(int, SmEventType, SmErrType, void *) {}

  RpcTest() {
    nexus = new Nexus("localhost", kUdpPort, kNumBgThreads);
    rt_assert(nexus != nullptr, "RpcTest: Failed to create nexus");
    nexus->drop_all_rx();

    rpc = new Rpc<IBTransport>(nexus, nullptr, kRpcId, sm_handler, kPhyPort,
                               kNumaNode);
    rt_assert(rpc != nullptr, "RpcTest: Failed to create Rpc");

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
    se.transport_type = transport_type;
    strcpy(se.hostname, "localhost");
    se.phy_port = kPhyPort;
    se.rpc_id = rpc_id;
    se.session_num = session_num;

    // Any routing info that's resolvable is fine
    rpc->transport->fill_local_routing_info(&se.routing_info);
    return se;
  }

  Nexus *nexus = nullptr;
  Rpc<IBTransport> *rpc = nullptr;
};

TEST_F(RpcTest, handle_connect_req_st) {
  auto server = gen_session_endpoint(kRpcId, kInvalidSessionNum);
  auto client = gen_session_endpoint(kRpcId + 1, /* session number */ 0);
  SmPkt sm_pkt(SmPktType::kConnectReq, SmErrType::kNoError, kUniqToken, client,
               server);

  rpc->handle_connect_req_st(sm_pkt);
  ASSERT_EQ(rpc->session_vec.size(), 1);
  ASSERT_EQ(rpc->sm_token_map.count(kUniqToken), 1);
}

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

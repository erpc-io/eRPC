#include "rpc.h"
#include <gtest/gtest.h>

namespace erpc {

// An Rpc with no established sessions
class RpcTest : public ::testing::Test {
 public:
  static constexpr size_t kUdpPort = 3185;
  static constexpr size_t kPhyPort = 0;
  static constexpr size_t kRpcId = 0;
  static constexpr size_t kNumBgThreads = 0;
  static constexpr size_t kNumaNode = 0;
  static constexpr auto transport_type = Transport::TransportType::kInfiniBand;

  static void sm_handler(int, SmEventType, SmErrType, void *) {}

  RpcTest()
      : general_endpoint(transport_type, "localhost", kPhyPort, kRpcId,
                         kInvalidSessionNum) {
    nexus = new Nexus("localhost", kUdpPort, kNumBgThreads);
    rt_assert(nexus != nullptr, "RpcTest: Failed to create nexus");

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

  Nexus *nexus = nullptr;
  Rpc<IBTransport> *rpc = nullptr;

  // This RPC endpoint without session number
  const SessionEndpoint general_endpoint;
};

TEST_F(RpcTest, handle_connect_req_st) {
  auto client = SessionEndpoint(Transport::TransportType::kInfiniBand,
                                "localhost", kPhyPort, kRpcId + 1, 0);
  const sm_uniq_token_t uniq_token = 0;

  SmPkt sm_pkt(SmPktType::kConnectReq, SmErrType::kNoError, uniq_token, client,
               general_endpoint);
}

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

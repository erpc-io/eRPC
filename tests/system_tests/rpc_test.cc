#include "rpc.h"
#include <gtest/gtest.h>

namespace erpc {

// Session management tests
class RpcTest : public ::testing::Test {
 public:
  static constexpr size_t kUdpPort = 3185;
  static constexpr size_t kPhyPort = 0;
  static constexpr size_t kClientRpcId = 0;
  static constexpr size_t kServerRpcId = 1;
  static constexpr size_t kNumBgThreads = 0;
  static constexpr size_t kNumaNode = 0;

  static void sm_handler(int, SmEventType, SmErrType, void *) {}

  RpcTest() {
    nexus = new Nexus("localhost", kUdpPort, kNumBgThreads);
    rt_assert(nexus != nullptr, "RpcTest: Failed to create nexus");

    rpc = new Rpc<IBTransport>(nexus, nullptr, kClientRpcId, sm_handler,
                               kPhyPort, kNumaNode);
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
};

TEST_F(RpcTest, handle_connect_req_st) { ASSERT_NE(rpc, nullptr); }

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

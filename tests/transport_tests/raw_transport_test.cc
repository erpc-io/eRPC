#include <gtest/gtest.h>

#define private public  // XXX: Do we need this
#include "transport_impl/raw/raw_transport.h"
#include "util/huge_alloc.h"

namespace erpc {
static constexpr size_t kTestPhyPort = 0;
static constexpr size_t kTestRpcId = 0;
static constexpr size_t kTestNumaNode = 0;

class RawTransportTest : public ::testing::Test {
 public:
  RawTransportTest() {
    if (!kTesting) {
      fprintf(stderr, "Cannot run tests - kTesting is disabled.\n");
      return;
    }

    transport = new RawTransport(kTestRpcId, kTestPhyPort, kTestNumaNode);

    huge_alloc = new HugeAlloc(MB(32), kTestNumaNode, transport->reg_mr_func,
                               transport->dereg_mr_func);

    transport->init_hugepage_structures(huge_alloc, rx_ring);
  }

  ~RawTransportTest() {
    delete transport;
    delete huge_alloc;
  }

  uint8_t *rx_ring[RawTransport::kNumRxRingEntries];
  HugeAlloc *huge_alloc;
  RawTransport *transport;
};

// Test if we we can create and destroy a transport instance
TEST_F(RawTransportTest, create) {}

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

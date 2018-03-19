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

    // Initalize the transport
    transport = new RawTransport(kTestRpcId, kTestPhyPort, kTestNumaNode);

    huge_alloc = new HugeAlloc(MB(32), kTestNumaNode, transport->reg_mr_func,
                               transport->dereg_mr_func);

    transport->init_hugepage_structures(huge_alloc, rx_ring);

    // Initialize TX msgbufs. All packets are sent to self.
    transport->fill_local_routing_info(&self_ri);
    transport->resolve_remote_routing_info(&self_ri);

    for (size_t i = 0; i < RawTransport::kPostlist; i++) {
      Buffer buf = huge_alloc->alloc(RawTransport::kMTU);
      assert(buf.buf != nullptr);

      tx_msgbuf[i] = MsgBuffer(buf, RawTransport::kMTU - sizeof(pkthdr_t), 1);

      tx_burst_arr[i].routing_info = &self_ri;
      tx_burst_arr[i].msg_buffer = &tx_msgbuf[i];
      tx_burst_arr[i].pkt_index = 0;
      tx_burst_arr[i].tx_ts = &tx_ts[i];
      tx_burst_arr[i].drop = false;
    }
  }

  ~RawTransportTest() {
    delete huge_alloc;
    delete transport;
  }

  HugeAlloc *huge_alloc;
  RawTransport *transport;
  uint8_t *rx_ring[RawTransport::kNumRxRingEntries];

  Transport::RoutingInfo self_ri;
  Transport::tx_burst_item_t tx_burst_arr[RawTransport::kPostlist];
  MsgBuffer tx_msgbuf[RawTransport::kPostlist];
  size_t tx_ts[RawTransport::kPostlist];
};

// Test if we we can create and destroy a transport instance
TEST_F(RawTransportTest, create) {}

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

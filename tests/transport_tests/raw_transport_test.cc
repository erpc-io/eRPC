#include <gtest/gtest.h>

#define private public  // XXX: Do we need this
#include "transport_impl/raw/raw_transport.h"
#include "util/huge_alloc.h"

namespace erpc {
static constexpr size_t kTestPhyPort = 0;
static constexpr size_t kTestRpcId = 0;
static constexpr size_t kTestNumaNode = 0;

static constexpr size_t kTestSmallMsgSize = 32;   // Data in small messages
static constexpr size_t kTestLargeMsgSize = 900;  // Data in large messages

class RawTransportTest : public ::testing::Test {
 public:
  RawTransportTest() {
    if (!kTesting) {
      fprintf(stderr, "Cannot run tests - kTesting is disabled.\n");
      return;
    }

    if (RawTransport::kDumb) {
      fprintf(stderr, "Dumbpipe mode not tested yet.\n");
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
    printf("Self routing info = %s\n",
           RawTransport::routing_info_str(&self_ri).c_str());

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

  HugeAlloc* huge_alloc;
  RawTransport* transport;
  uint8_t* rx_ring[RawTransport::kNumRxRingEntries];

  Transport::RoutingInfo self_ri;
  Transport::tx_burst_item_t tx_burst_arr[RawTransport::kPostlist];
  MsgBuffer tx_msgbuf[RawTransport::kPostlist];
  size_t tx_ts[RawTransport::kPostlist];

  // Create a ready-to-send packet with destination = self, and with space for
  // \p data_size user data bytes
  Buffer create_packet(size_t data_size) {
    size_t pkt_size = kInetHdrsTotSize + data_size;
    Buffer buffer = huge_alloc->alloc(pkt_size);  // Get a registered buffer
    assert(buffer.buf != nullptr);

    memset(buffer.buf, 0, pkt_size);
    auto* pkthdr = reinterpret_cast<pkthdr_t*>(buffer.buf);

    memcpy(&pkthdr->headroom[0], &self_ri, sizeof(Transport::RoutingInfo));

    auto* ipv4_hdr =
        reinterpret_cast<ipv4_hdr_t*>(&pkthdr->headroom[sizeof(eth_hdr_t)]);
    assert(ipv4_hdr->check == 0);
    ipv4_hdr->tot_len = htons(pkt_size - sizeof(eth_hdr_t));
    ipv4_hdr->dst_ip = 0;  // Dropped by switch, fast

    auto* udp_hdr = reinterpret_cast<udp_hdr_t*>(&ipv4_hdr[1]);
    assert(udp_hdr->check == 0);
    udp_hdr->len = htons(pkt_size - sizeof(eth_hdr_t) - sizeof(ipv4_hdr_t));

    return buffer;
  }
};

TEST_F(RawTransportTest, create) {
  // Test if we we can create and destroy a transport instance
}

TEST_F(RawTransportTest, one_small_tx) {
  Buffer buffer = create_packet(kTestSmallMsgSize);

  struct ibv_sge sge;
  sge.addr = reinterpret_cast<uint64_t>(buffer.buf);
  sge.length = kInetHdrsTotSize + kTestSmallMsgSize;
  sge.lkey = buffer.lkey;

  struct ibv_send_wr send_wr;
  send_wr.next = nullptr;
  send_wr.opcode = IBV_WR_SEND;
  send_wr.sg_list = &sge;
  send_wr.send_flags = IBV_SEND_SIGNALED;
  send_wr.num_sge = 1;

  struct ibv_send_wr* bad_wr;
  int ret = ibv_post_send(transport->qp, &send_wr, &bad_wr);
  ASSERT_EQ(ret, 0);

  poll_cq_one_helper(transport->send_cq);
  poll_cq_one_helper(transport->recv_cq);

  huge_alloc->free_buf(buffer);
}

}  // End erpc

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

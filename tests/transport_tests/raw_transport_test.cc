// Ideally these tests would use a loopback, but loopback doesn't work for some
// reason.

#include <gtest/gtest.h>

#define private public  // XXX: Do we need this
#include "transport_impl/raw/raw_transport.h"
#include "util/huge_alloc.h"

namespace erpc {
static constexpr size_t kTestPhyPort = 0;
static constexpr size_t kTestRpcIdClient = 100;
static constexpr size_t kTestRpcIdServer = 200;
static constexpr size_t kTestNumaNode = 0;

static constexpr size_t kTestSmallMsgSize = 32;   // Data in small messages
static constexpr size_t kTestLargeMsgSize = 900;  // Data in large messages

struct transport_info_t {
  HugeAlloc* huge_alloc;
  RawTransport* transport;
  uint8_t* rx_ring[RawTransport::kNumRxRingEntries];
};

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

    // Initalize client transport
    clt_ttr.transport =
        new RawTransport(kTestRpcIdClient, kTestPhyPort, kTestNumaNode);
    clt_ttr.huge_alloc =
        new HugeAlloc(MB(32), kTestNumaNode, clt_ttr.transport->reg_mr_func,
                      clt_ttr.transport->dereg_mr_func);
    clt_ttr.transport->init_hugepage_structures(clt_ttr.huge_alloc,
                                                clt_ttr.rx_ring);

    // Initialize server transport
    srv_ttr.transport =
        new RawTransport(kTestRpcIdServer, kTestPhyPort, kTestNumaNode);
    srv_ttr.huge_alloc =
        new HugeAlloc(MB(32), kTestNumaNode, srv_ttr.transport->reg_mr_func,
                      srv_ttr.transport->dereg_mr_func);
    srv_ttr.transport->init_hugepage_structures(srv_ttr.huge_alloc,
                                                srv_ttr.rx_ring);

    // Precompute the UDP header for the server
    srv_ttr.transport->fill_local_routing_info(&srv_ri);
    clt_ttr.transport->resolve_remote_routing_info(&srv_ri);
  }

  ~RawTransportTest() {
    delete clt_ttr.huge_alloc;
    delete clt_ttr.transport;

    delete srv_ttr.huge_alloc;
    delete srv_ttr.transport;
  }

  transport_info_t srv_ttr, clt_ttr;
  Transport::RoutingInfo srv_ri;  // We only need the server's routing info

  // Create a ready-to-send packet from client to server, with space for
  // \p data_size user data bytes
  Buffer create_packet(size_t data_size) {
    size_t pkt_size = kInetHdrsTotSize + data_size;
    Buffer buffer = clt_ttr.huge_alloc->alloc(pkt_size);
    assert(buffer.buf != nullptr);

    memset(buffer.buf, 0, pkt_size);
    auto* pkthdr = reinterpret_cast<pkthdr_t*>(buffer.buf);

    memcpy(&pkthdr->headroom[0], &srv_ri, sizeof(Transport::RoutingInfo));

    auto* ipv4_hdr =
        reinterpret_cast<ipv4_hdr_t*>(&pkthdr->headroom[sizeof(eth_hdr_t)]);
    ipv4_hdr->tot_len = htons(pkt_size - sizeof(eth_hdr_t));

    auto* udp_hdr = reinterpret_cast<udp_hdr_t*>(&ipv4_hdr[1]);
    udp_hdr->len = htons(pkt_size - sizeof(eth_hdr_t) - sizeof(ipv4_hdr_t));

    return buffer;
  }
};

TEST_F(RawTransportTest, create) {
  // Test if we we can create and destroy a transport instance
}

TEST_F(RawTransportTest, one_small_tx) {
  Buffer buffer = create_packet(kTestSmallMsgSize);

  printf("Sending packet. Frame header = %s.\n",
         frame_header_to_string(&buffer.buf[0]).c_str());

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
  int ret = ibv_post_send(clt_ttr.transport->qp, &send_wr, &bad_wr);
  ASSERT_EQ(ret, 0);

  poll_cq_one_helper(clt_ttr.transport->send_cq);
  poll_cq_one_helper(srv_ttr.transport->recv_cq);
}

}  // End erpc

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

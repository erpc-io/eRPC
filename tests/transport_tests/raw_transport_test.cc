/**
 * @file raw_transport_test.cc
 * @brief Tests for Mellanox Raw transport implementation
 *
 * Ideally these tests would use only one RawTransport instance and loopback,
 * but I couldn't get loopback to work. Two RawTransport instances are used as
 * a workaround.
 */

#ifdef RAW

#include <gtest/gtest.h>

#define private public
#include "transport_impl/raw/raw_transport.h"
#include "util/huge_alloc.h"
#include "util/timer.h"

namespace erpc {
static constexpr size_t kTestSmUdpPort = kBaseSmUdpPort;
static constexpr size_t kTestPhyPort = 0;
static constexpr size_t kTestRpcIdClient = 100;
static constexpr size_t kTestRpcIdServer = 200;
static constexpr size_t kTestNumaNode = 0;

static constexpr size_t kTestMsgSize = 500;  // Data in each message

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

    std::string trace_filename = "/tmp/test_trace";
    trace_file = fopen(trace_filename.c_str(), "w");
    assert(trace_file != nullptr);

    // Initalize client transport
    clt_ttr.transport =
        new RawTransport(kTestSmUdpPort, kTestRpcIdClient, kTestPhyPort,
                         kTestNumaNode, trace_file);
    clt_ttr.huge_alloc =
        new HugeAlloc(MB(32), kTestNumaNode, clt_ttr.transport->reg_mr_func,
                      clt_ttr.transport->dereg_mr_func);
    clt_ttr.transport->init_hugepage_structures(clt_ttr.huge_alloc,
                                                clt_ttr.rx_ring);

    // Initialize server transport
    srv_ttr.transport =
        new RawTransport(kTestSmUdpPort, kTestRpcIdServer, kTestPhyPort,
                         kTestNumaNode, trace_file);
    srv_ttr.huge_alloc =
        new HugeAlloc(MB(32), kTestNumaNode, srv_ttr.transport->reg_mr_func,
                      srv_ttr.transport->dereg_mr_func);
    srv_ttr.transport->init_hugepage_structures(srv_ttr.huge_alloc,
                                                srv_ttr.rx_ring);

    // Precompute the packet header, pretending that srv_ttr is the remote.
    srv_ttr.transport->fill_local_routing_info(&srv_ri);
    clt_ttr.transport->resolve_remote_routing_info(&srv_ri);  // srv_ri ~ header
  }

  ~RawTransportTest() {
    delete clt_ttr.huge_alloc;
    delete clt_ttr.transport;

    delete srv_ttr.huge_alloc;
    delete srv_ttr.transport;
  }

  // Create a ready-to-send packet from client to server, with space for
  // \p data_size user data bytes. \p data_size can be zero.
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

  /**
   * @brief Client transmits a batch of same-length packets, each in one SGE.
   * Then client checks for all SEND completions, and server for all RECV
   * completions.
   */
  void simple_test_one_sge(size_t data_size, size_t batch_size) {
    rt_assert(kInetHdrsTotSize + data_size < RawTransport::kMTU);
    rt_assert(batch_size <= RawTransport::kSQDepth, "Batch size too large");

    Buffer buffer = create_packet(data_size);

    struct ibv_sge sge[RawTransport::kSQDepth];
    struct ibv_send_wr send_wr[RawTransport::kSQDepth + 1];

    for (size_t i = 0; i < batch_size; i++) {
      sge[i].addr = reinterpret_cast<uint64_t>(buffer.buf);
      sge[i].length = kInetHdrsTotSize + data_size;
      sge[i].lkey = buffer.lkey;

      send_wr[i].next = &send_wr[i + 1];
      send_wr[i].opcode = IBV_WR_SEND;
      send_wr[i].sg_list = &sge[i];
      send_wr[i].send_flags = IBV_SEND_SIGNALED;
      send_wr[i].num_sge = 1;
    }
    send_wr[batch_size - 1].next = nullptr;

    struct ibv_send_wr* bad_wr;
    int ret = ibv_post_send(clt_ttr.transport->qp, &send_wr[0], &bad_wr);
    ASSERT_EQ(ret, 0);

    for (size_t i = 0; i < batch_size; i++) {
      poll_cq_one_helper(clt_ttr.transport->send_cq);
    }

    size_t num_rx = 0;
    while (num_rx != batch_size) num_rx += srv_ttr.transport->rx_burst();
  }

  /**
   * @brief Client transmits a batch of same-length packets, each in two SGEs.
   * Then client checks for all SEND completions, and server for all RECV
   * completions.
   *
   * The first SGE contains only the Ethernet header, the second only the data.
   */
  void simple_test_two_sges(size_t data_size, size_t batch_size) {
    rt_assert(kInetHdrsTotSize + data_size < RawTransport::kMTU);
    rt_assert(batch_size <= RawTransport::kSQDepth, "Batch size too large");

    Buffer hdr_buffer = create_packet(0);  // Allocates space for one header
    Buffer data_buffer = clt_ttr.huge_alloc->alloc(data_size);

    struct ibv_sge sge[RawTransport::kSQDepth][2];
    struct ibv_send_wr send_wr[RawTransport::kSQDepth + 1];

    for (size_t i = 0; i < batch_size; i++) {
      sge[i][0].addr = reinterpret_cast<uint64_t>(hdr_buffer.buf);
      sge[i][0].length = kInetHdrsTotSize;
      sge[i][0].lkey = hdr_buffer.lkey;

      sge[i][1].addr = reinterpret_cast<uint64_t>(data_buffer.buf);
      sge[i][1].length = data_size;
      sge[i][1].lkey = data_buffer.lkey;

      send_wr[i].next = &send_wr[i + 1];
      send_wr[i].opcode = IBV_WR_SEND;
      send_wr[i].sg_list = &sge[i][0];
      send_wr[i].send_flags = IBV_SEND_SIGNALED;
      send_wr[i].num_sge = 2;
    }
    send_wr[batch_size - 1].next = nullptr;

    struct ibv_send_wr* bad_wr;
    int ret = ibv_post_send(clt_ttr.transport->qp, &send_wr[0], &bad_wr);
    ASSERT_EQ(ret, 0);

    for (size_t i = 0; i < batch_size; i++) {
      poll_cq_one_helper(clt_ttr.transport->send_cq);
    }

    size_t num_rx = 0;
    while (num_rx != batch_size) num_rx += srv_ttr.transport->rx_burst();
  }

  transport_info_t srv_ttr, clt_ttr;
  Transport::RoutingInfo srv_ri;  // We only need the server's routing info
  FILE* trace_file;
};

// Test if we we can create and destroy a transport instance
TEST_F(RawTransportTest, create) {}

// One packet in one SGE
TEST_F(RawTransportTest, one_packet_one_sge) {
  simple_test_one_sge(kTestMsgSize, 1);
}

// One packet in two SGEs
TEST_F(RawTransportTest, one_packet_two_sges) {
  simple_test_two_sges(kTestMsgSize, 1);
}

// A full queue of packets, each in one SGE
TEST_F(RawTransportTest, full_queue_one_sge) {
  simple_test_one_sge(kTestMsgSize, RawTransport::kSQDepth);
}

// A full queue of packets, each in two SGEs
TEST_F(RawTransportTest, full_queue_two_sges) {
  simple_test_two_sges(kTestMsgSize, RawTransport::kSQDepth);
}

// Test MAC filtering
TEST_F(RawTransportTest, mac_filter_test) {
  Buffer buffer = create_packet(kTestMsgSize);
  struct ibv_sge sge;
  struct ibv_send_wr send_wr, *bad_wr;
  size_t num_rx = 0;

  sge.addr = reinterpret_cast<uint64_t>(buffer.buf);
  sge.length = kInetHdrsTotSize + kTestMsgSize;
  sge.lkey = buffer.lkey;

  send_wr.next = nullptr;
  send_wr.opcode = IBV_WR_SEND;
  send_wr.sg_list = &sge;
  send_wr.send_flags = IBV_SEND_SIGNALED;
  send_wr.num_sge = 1;

  // Send a packet with a matching MAC address
  int ret = ibv_post_send(clt_ttr.transport->qp, &send_wr, &bad_wr);
  assert(ret == 0);
  while (num_rx != 1) num_rx += srv_ttr.transport->rx_burst();

  // Send a packet with a garbled destination MAC address and check it isn't
  // received for 200 milliseconds
  num_rx = 0;
  auto* eth = reinterpret_cast<eth_hdr_t*>(buffer.buf);
  eth->dst_mac[0]++;

  ret = ibv_post_send(clt_ttr.transport->qp, &send_wr, &bad_wr);
  assert(ret == 0);
  struct timespec start;
  clock_gettime(CLOCK_REALTIME, &start);
  while (sec_since(start) <= .2) {
    num_rx += srv_ttr.transport->rx_burst();
  }
  ASSERT_EQ(num_rx, 0);
}
}  // namespace erpc

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#endif

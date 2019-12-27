#include "protocol_tests.h"

namespace erpc {

// Ensure that we have enough packets to fill one credit window
static_assert(kTestLargeMsgSize / CTransport::kMTU > kSessionCredits, "");

/// Common setup code for client kick tests
class RpcClientKickTest : public RpcTest {
 public:
  SessionEndpoint client, server;
  Session *clt_session;
  SSlot *sslot_0;
  MsgBuffer req, resp;

  RpcClientKickTest() {
    client = get_local_endpoint();
    server = get_remote_endpoint();
    clt_session = create_client_session_connected(client, server);
    sslot_0 = &clt_session->sslot_arr[0];

    req = rpc->alloc_msg_buffer(kTestLargeMsgSize);
    resp = rpc->alloc_msg_buffer(kTestLargeMsgSize);
  }
};

/// Transmit all sslots in a wheel. Return number of packets transmitted.
size_t wheel_tx_all(Rpc<CTransport> *rpc) {
  for (size_t i = 0; i < kWheelNumWslots; i++) rpc->wheel->reap_wslot(i);
  size_t ret = rpc->wheel->ready_queue.size();
  rpc->process_wheel_st();
  return ret;
}

/// Kicking a sslot without credits is disallowed
TEST_F(RpcClientKickTest, kick_st_no_credits) {
  rpc->enqueue_request(0, kTestReqType, &req, &resp, cont_func, kTestTag);
  assert(clt_session->client_info.credits == 0);
  ASSERT_DEATH(rpc->kick_req_st(sslot_0), ".*");
}

/// Kicking a sslot that has transmitted all request packets but received no
/// response packet is disallowed
TEST_F(RpcClientKickTest, kick_st_all_request_no_response) {
  rpc->enqueue_request(0, kTestReqType, &req, &resp, cont_func, kTestTag);
  assert(clt_session->client_info.credits == 0);
  sslot_0->client_info.num_tx = rpc->data_size_to_num_pkts(req.data_size);
  sslot_0->client_info.num_rx = sslot_0->client_info.num_tx - kSessionCredits;
  ASSERT_DEATH(rpc->kick_req_st(sslot_0), ".*");
}

/// Kicking a sslot that has received the full response is disallowed
TEST_F(RpcClientKickTest, kick_st_full_response) {
  rpc->enqueue_request(0, kTestReqType, &req, &resp, cont_func, kTestTag);
  assert(clt_session->client_info.credits == 0);

  *resp.get_pkthdr_0() = *req.get_pkthdr_0();  // Match request's formatted hdr
  sslot_0->client_info.resp_msgbuf = &resp;

  sslot_0->client_info.num_tx = rpc->wire_pkts(&req, &resp);
  sslot_0->client_info.num_rx = rpc->wire_pkts(&req, &resp);
  clt_session->client_info.credits = kSessionCredits;

  ASSERT_DEATH(rpc->kick_rfr_st(sslot_0), ".*");
}

/// Kick a sslot that hasn't transmitted all request packets
TEST_F(RpcClientKickTest, kick_st_req_pkts) {
  // This test requires the timing wheel to be enabled
  assert(rpc->faults.hard_wheel_bypass == false);
  assert(kEnableCc == true);

  // enqueue_request() calls kick_st()
  rpc->enqueue_request(0, kTestReqType, &req, &resp, cont_func, kTestTag);
  ASSERT_EQ(clt_session->client_info.credits, 0);
  ASSERT_EQ(sslot_0->client_info.num_tx, kSessionCredits);

  // Transmit the kSessionCredits request packets in the wheel
  ASSERT_EQ(wheel_tx_all(rpc), kSessionCredits);
  ASSERT_EQ(sslot_0->client_info.num_tx, kSessionCredits);

  // Pretend that a CR has been received
  clt_session->client_info.credits = 1;
  sslot_0->client_info.num_rx = 1;
  rpc->kick_req_st(sslot_0);
  ASSERT_EQ(clt_session->client_info.credits, 0);
  ASSERT_EQ(sslot_0->client_info.num_tx, kSessionCredits + 1);

  // Transmit the one request packet in the wheel
  pkthdr_tx_queue->clear();
  ASSERT_EQ(wheel_tx_all(rpc), 1);
  ASSERT_EQ(sslot_0->client_info.num_tx, kSessionCredits + 1);
  ASSERT_TRUE(
      pkthdr_tx_queue->pop().matches(PktType::kPktTypeReq, kSessionCredits));

  // Kicking twice in a row without any RX in between is disallowed
  ASSERT_DEATH(rpc->kick_req_st(sslot_0), ".*");
}

/// Kick a sslot that has received the first response packet
TEST_F(RpcClientKickTest, kick_st_rfr_pkts) {
  rpc->enqueue_request(0, kTestReqType, &req, &resp, cont_func, kTestTag);
  assert(clt_session->client_info.credits == 0);
  pkthdr_tx_queue->clear();

  *resp.get_pkthdr_0() = *req.get_pkthdr_0();  // Match request's formatted hdr
  sslot_0->client_info.resp_msgbuf = &resp;

  // Pretend we have received the first response
  const size_t req_npkts = rpc->data_size_to_num_pkts(req.data_size);
  sslot_0->client_info.num_tx = req_npkts;
  sslot_0->client_info.num_rx = req_npkts;
  clt_session->client_info.credits = kSessionCredits;

  rpc->kick_rfr_st(sslot_0);
  ASSERT_EQ(pkthdr_tx_queue->size(), kSessionCredits);

  for (size_t i = 0; i < kSessionCredits; i++) {
    ASSERT_TRUE(
        pkthdr_tx_queue->pop().matches(PktType::kPktTypeRFR, req_npkts + i));
  }
}

}  // namespace erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

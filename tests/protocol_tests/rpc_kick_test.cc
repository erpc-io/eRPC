#include "protocol_tests.h"

namespace erpc {

// Ensure that we have enough packets to fill one credit window
static_assert(kTestLargeMsgSize / CTransport::kMTU > kSessionCredits, "");

/// Common setup code for client kick tests
class RpcClientKickTest : public RpcTest {
 public:
  SessionEndpoint client_, server_;
  Session *clt_session_;
  SSlot *sslot_0_;
  MsgBuffer req_, resp_;

  RpcClientKickTest() {
    client_ = get_local_endpoint();
    server_ = get_remote_endpoint();
    clt_session_ = create_client_session_connected(client_, server_);
    sslot_0_ = &clt_session_->sslot_arr_[0];

    req_ = rpc_->alloc_msg_buffer(kTestLargeMsgSize);
    resp_ = rpc_->alloc_msg_buffer(kTestLargeMsgSize);
  }
};

/// Transmit all sslots in a wheel. Return number of packets transmitted.
size_t wheel_tx_all(Rpc<CTransport> *rpc) {
  for (size_t i = 0; i < kWheelNumWslots; i++) rpc->wheel_->reap_wslot(i);
  size_t ret = rpc->wheel_->ready_queue_.size();
  rpc->process_wheel_st();
  return ret;
}

/// Kicking a sslot without credits is disallowed
TEST_F(RpcClientKickTest, kick_st_no_credits) {
  rpc_->enqueue_request(0, kTestReqType, &req_, &resp_, cont_func, kTestTag);
  assert(clt_session_->client_info_.credits_ == 0);
  ASSERT_DEATH(rpc_->kick_req_st(sslot_0_), ".*");
}

/// Kicking a sslot that has transmitted all request packets but received no
/// response packet is disallowed
TEST_F(RpcClientKickTest, kick_st_all_request_no_response) {
  rpc_->enqueue_request(0, kTestReqType, &req_, &resp_, cont_func, kTestTag);
  assert(clt_session_->client_info_.credits_ == 0);
  sslot_0_->client_info_.num_tx_ = rpc_->data_size_to_num_pkts(req_.data_size_);
  sslot_0_->client_info_.num_rx_ =
      sslot_0_->client_info_.num_tx_ - kSessionCredits;
  ASSERT_DEATH(rpc_->kick_req_st(sslot_0_), ".*");
}

/// Kicking a sslot that has received the full response is disallowed
TEST_F(RpcClientKickTest, kick_st_full_response) {
  rpc_->enqueue_request(0, kTestReqType, &req_, &resp_, cont_func, kTestTag);
  assert(clt_session_->client_info_.credits_ == 0);

  *resp_.get_pkthdr_0() =
      *req_.get_pkthdr_0();  // Match request's formatted hdr
  sslot_0_->client_info_.resp_msgbuf_ = &resp_;

  sslot_0_->client_info_.num_tx_ = rpc_->wire_pkts(&req_, &resp_);
  sslot_0_->client_info_.num_rx_ = rpc_->wire_pkts(&req_, &resp_);
  clt_session_->client_info_.credits_ = kSessionCredits;

  ASSERT_DEATH(rpc_->kick_rfr_st(sslot_0_), ".*");
}

/// Kick a sslot that hasn't transmitted all request packets
TEST_F(RpcClientKickTest, kick_st_req_pkts) {
  // This test requires the timing wheel to be enabled
  assert(rpc_->faults_.hard_wheel_bypass_ == false);
  assert(kEnableCc == true);

  // enqueue_request() calls kick_st()
  rpc_->enqueue_request(0, kTestReqType, &req_, &resp_, cont_func, kTestTag);
  ASSERT_EQ(clt_session_->client_info_.credits_, 0);
  ASSERT_EQ(sslot_0_->client_info_.num_tx_, kSessionCredits);

  // Transmit the kSessionCredits request packets in the wheel
  ASSERT_EQ(wheel_tx_all(rpc_), kSessionCredits);
  ASSERT_EQ(sslot_0_->client_info_.num_tx_, kSessionCredits);

  // Pretend that a CR has been received
  clt_session_->client_info_.credits_ = 1;
  sslot_0_->client_info_.num_rx_ = 1;
  rpc_->kick_req_st(sslot_0_);
  ASSERT_EQ(clt_session_->client_info_.credits_, 0);
  ASSERT_EQ(sslot_0_->client_info_.num_tx_, kSessionCredits + 1);

  // Transmit the one request packet in the wheel
  pkthdr_tx_queue_->clear();
  ASSERT_EQ(wheel_tx_all(rpc_), 1);
  ASSERT_EQ(sslot_0_->client_info_.num_tx_, kSessionCredits + 1);
  ASSERT_TRUE(pkthdr_tx_queue_->pop().matches(PktType::kReq, kSessionCredits));

  // Kicking twice in a row without any RX in between is disallowed
  ASSERT_DEATH(rpc_->kick_req_st(sslot_0_), ".*");
}

/// Kick a sslot that has received the first response packet
TEST_F(RpcClientKickTest, kick_st_rfr_pkts) {
  rpc_->enqueue_request(0, kTestReqType, &req_, &resp_, cont_func, kTestTag);
  assert(clt_session_->client_info_.credits_ == 0);
  pkthdr_tx_queue_->clear();

  *resp_.get_pkthdr_0() =
      *req_.get_pkthdr_0();  // Match request's formatted hdr
  sslot_0_->client_info_.resp_msgbuf_ = &resp_;

  // Pretend we have received the first response
  const size_t req_npkts = rpc_->data_size_to_num_pkts(req_.data_size_);
  sslot_0_->client_info_.num_tx_ = req_npkts;
  sslot_0_->client_info_.num_rx_ = req_npkts;
  clt_session_->client_info_.credits_ = kSessionCredits;

  rpc_->kick_rfr_st(sslot_0_);
  ASSERT_EQ(pkthdr_tx_queue_->size(), kSessionCredits);

  for (size_t i = 0; i < kSessionCredits; i++) {
    ASSERT_TRUE(pkthdr_tx_queue_->pop().matches(PktType::kRFR, req_npkts + i));
  }
}

}  // namespace erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

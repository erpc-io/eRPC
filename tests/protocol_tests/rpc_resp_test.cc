#include "protocol_tests.h"

namespace erpc {

TEST_F(RpcTest, process_resp_one_SMALL_st) {
  const auto client = get_local_endpoint();
  const auto server = get_remote_endpoint();
  Session *clt_session = create_client_session_connected(client, server);
  SSlot *sslot_0 = &clt_session->sslot_arr_[0];

  MsgBuffer req = rpc_->alloc_msg_buffer(kTestSmallMsgSize);
  MsgBuffer local_resp = rpc_->alloc_msg_buffer(kTestSmallMsgSize);

  // Use enqueue_request() to do sslot formatting for the request. Small request
  // is sent right away, so it uses credits.
  rpc_->faults_.hard_wheel_bypass_ = true;  // Don't place request pkt in wheel
  rpc_->enqueue_request(0, kTestReqType, &req, &local_resp, cont_func, kTestTag);
  assert(clt_session->client_info_.credits_ == kSessionCredits - 1);
  assert(sslot_0->client_info_.num_tx_ == 1);

  // Construct the basic test response packet
  uint8_t remote_resp[sizeof(pkthdr_t) + kTestSmallMsgSize];
  auto *pkthdr_0 = reinterpret_cast<pkthdr_t *>(remote_resp);
  pkthdr_0->format(kTestReqType, kTestSmallMsgSize, client.session_num_,
                   PktType::kResp, 0 /* pkt_num */, kSessionReqWindow);

  size_t batch_rx_tsc = rdtsc();  // Stress batch TSC use

  // Receive an old response (past)
  // Expect: It's dropped
  assert(sslot_0->cur_req_num_ == kSessionReqWindow);
  sslot_0->cur_req_num_ += kSessionReqWindow;
  rpc_->process_resp_one_st(sslot_0, pkthdr_0, batch_rx_tsc);
  ASSERT_EQ(sslot_0->client_info_.num_rx_, 0);
  ASSERT_EQ(num_cont_func_calls_, 0);
  sslot_0->cur_req_num_ -= kSessionReqWindow;

  // Receive resp while request progress is rolled back (roll-back)
  // Expect: It's dropped.
  assert(sslot_0->client_info_.num_tx_ == 1);
  sslot_0->client_info_.num_tx_ = 0;
  rpc_->process_resp_one_st(sslot_0, pkthdr_0, batch_rx_tsc);
  ASSERT_EQ(num_cont_func_calls_, 0);
  sslot_0->client_info_.num_tx_ = 1;

  // Receive an in-order small response (in-order)
  // Expect: Continuation is invoked.
  rpc_->process_resp_one_st(sslot_0, pkthdr_0, batch_rx_tsc);
  ASSERT_EQ(num_cont_func_calls_, 1);
  ASSERT_EQ(sslot_0->tx_msgbuf_, nullptr);  // Response received
  num_cont_func_calls_ = 0;

  // Receive the same response again (past)
  // Expect: It's dropped
  rpc_->process_resp_one_st(sslot_0, pkthdr_0, batch_rx_tsc);
  ASSERT_EQ(num_cont_func_calls_, 0);
}

TEST_F(RpcTest, process_resp_one_LARGE_st) {
  // TODO
}

}  // namespace erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#include "protocol_tests.h"

namespace erpc {

TEST_F(RpcTest, process_resp_one_SMALL_st) {
  const auto client = get_local_endpoint();
  const auto server = get_remote_endpoint();
  Session *clt_session = create_client_session_connected(client, server);
  SSlot *sslot_0 = &clt_session->sslot_arr[0];

  MsgBuffer req = rpc->alloc_msg_buffer(kTestSmallMsgSize);
  MsgBuffer local_resp = rpc->alloc_msg_buffer(kTestSmallMsgSize);

  // Use enqueue_request() to do sslot formatting for the request. Small request
  // is sent right away, so it uses credits.
  rpc->faults.hard_wheel_bypass = true;  // Don't place request pkt in wheel
  rpc->enqueue_request(0, kTestReqType, &req, &local_resp, cont_func, kTestTag);
  assert(clt_session->client_info.credits == kSessionCredits - 1);
  assert(sslot_0->client_info.num_tx == 1);

  // Construct the basic test response packet
  uint8_t remote_resp[sizeof(pkthdr_t) + kTestSmallMsgSize];
  auto *pkthdr_0 = reinterpret_cast<pkthdr_t *>(remote_resp);
  pkthdr_0->format(kTestReqType, kTestSmallMsgSize, client.session_num,
                   PktType::kPktTypeResp, 0 /* pkt_num */, kSessionReqWindow);

  size_t batch_rx_tsc = rdtsc();  // Stress batch TSC use

  // Receive an old response (past)
  // Expect: It's dropped
  assert(sslot_0->cur_req_num == kSessionReqWindow);
  sslot_0->cur_req_num += kSessionReqWindow;
  rpc->process_resp_one_st(sslot_0, pkthdr_0, batch_rx_tsc);
  ASSERT_EQ(sslot_0->client_info.num_rx, 0);
  ASSERT_EQ(num_cont_func_calls, 0);
  sslot_0->cur_req_num -= kSessionReqWindow;

  // Receive resp while request progress is rolled back (roll-back)
  // Expect: It's dropped.
  assert(sslot_0->client_info.num_tx == 1);
  sslot_0->client_info.num_tx = 0;
  rpc->process_resp_one_st(sslot_0, pkthdr_0, batch_rx_tsc);
  ASSERT_EQ(num_cont_func_calls, 0);
  sslot_0->client_info.num_tx = 1;

  // Receive an in-order small response (in-order)
  // Expect: Continuation is invoked.
  rpc->process_resp_one_st(sslot_0, pkthdr_0, batch_rx_tsc);
  ASSERT_EQ(num_cont_func_calls, 1);
  ASSERT_EQ(sslot_0->tx_msgbuf, nullptr);  // Response received
  num_cont_func_calls = 0;

  // Receive the same response again (past)
  // Expect: It's dropped
  rpc->process_resp_one_st(sslot_0, pkthdr_0, batch_rx_tsc);
  ASSERT_EQ(num_cont_func_calls, 0);
}

TEST_F(RpcTest, process_resp_one_LARGE_st) {
  // TODO
}

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#include "protocol_tests.h"

namespace erpc {

TEST_F(RpcTest, process_expl_cr_st) {
  const auto client = get_local_endpoint();
  const auto server = get_remote_endpoint();
  Session *clt_session = create_client_session_connected(client, server);
  SSlot *sslot_0 = &clt_session->sslot_arr_[0];

  // Ensure that we have enough packets to fill one credit window
  static_assert(kTestLargeMsgSize / CTransport::kMTU > kSessionCredits, "");

  MsgBuffer req = rpc_->alloc_msg_buffer(kTestLargeMsgSize);
  MsgBuffer resp = rpc_->alloc_msg_buffer(kTestSmallMsgSize);  // Unused
  rpc_->faults_.hard_wheel_bypass_ = true;  // Don't place request pkts in wheel

  // Use enqueue_request() to do sslot formatting. This uses all credits.
  rpc_->enqueue_request(0, kTestReqType, &req, &resp, cont_func, kTestTag);
  assert(sslot_0->client_info_.num_tx_ == kSessionCredits);
  assert(clt_session->client_info_.credits_ == 0);
  assert(pkthdr_tx_queue_->size() == kSessionCredits);
  pkthdr_tx_queue_->clear();

  // Construct the basic explicit credit return packet
  pkthdr_t expl_cr;
  expl_cr.format(kTestReqType, 0 /* msg_size */, client.session_num_,
                 PktType::kExplCR, 0 /* pkt_num */, kSessionReqWindow);

  size_t batch_rx_tsc = rdtsc();  // Stress batch TSC use

  // Receive credit return for an old request (past)
  // Expect: It's dropped
  sslot_0->cur_req_num_ += kSessionReqWindow;
  rpc_->process_expl_cr_st(sslot_0, &expl_cr, batch_rx_tsc);
  ASSERT_EQ(sslot_0->client_info_.num_rx_, 0);
  sslot_0->cur_req_num_ -= kSessionReqWindow;

  // Receive the first in-order explicit credit return (in-order)
  // Expect: num_rx is bumped; another request packet is sent that uses a credit
  rpc_->process_expl_cr_st(sslot_0, &expl_cr, batch_rx_tsc);
  ASSERT_EQ(sslot_0->client_info_.num_rx_, 1);
  ASSERT_EQ(clt_session->client_info_.credits_, 0);
  ASSERT_TRUE(pkthdr_tx_queue_->pop().matches(PktType::kReq, kSessionCredits));

  // Receive the same explicit credit return again (past)
  // Expect: It's dropped
  rpc_->process_expl_cr_st(sslot_0, &expl_cr, batch_rx_tsc);
  ASSERT_EQ(sslot_0->client_info_.num_rx_, 1);

  // Client should use only num_rx & num_tx for ordering (sensitivity)
  // Expect: On resetting it, behavior should be like an in-order explicit CR
  sslot_0->client_info_.num_rx_--;
  sslot_0->client_info_.num_tx_--;
  rpc_->process_expl_cr_st(sslot_0, &expl_cr, batch_rx_tsc);
  ASSERT_EQ(sslot_0->client_info_.num_rx_, 1);
  ASSERT_EQ(clt_session->client_info_.credits_, 0);
  ASSERT_TRUE(pkthdr_tx_queue_->pop().matches(PktType::kReq, kSessionCredits));

  // Receive explicit credit return for a future pkt in this request (roll-back)
  // Expect: It's dropped
  expl_cr.pkt_num_ = 2;  // Future
  rpc_->process_expl_cr_st(sslot_0, &expl_cr, batch_rx_tsc);
  ASSERT_EQ(sslot_0->client_info_.num_rx_, 1);
  ASSERT_EQ(pkthdr_tx_queue_->size(), 0);
  expl_cr.pkt_num_ = 0;
}

}  // namespace erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

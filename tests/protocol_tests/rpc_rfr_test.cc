#include "protocol_tests.h"

namespace erpc {

TEST_F(RpcTest, process_rfr_st) {
  const auto server = get_local_endpoint();
  const auto client = get_remote_endpoint();
  Session *srv_session = create_server_session_init(client, server);
  SSlot *sslot_0 = &srv_session->sslot_arr_[0];

  const size_t k_num_req_pkts = 5;  // Size of the received request

  // Use enqueue_response() to do much of sslot formatting for the response
  sslot_0->server_info_.req_msgbuf_ =
      rpc_->alloc_msg_buffer(k_num_req_pkts * (rpc_->get_max_data_per_pkt()));
  sslot_0->server_info_.num_rx_ = k_num_req_pkts;

  sslot_0->cur_req_num_ = kSessionReqWindow;
  sslot_0->server_info_.req_type_ = kTestReqType;
  sslot_0->dyn_resp_msgbuf_ = rpc_->alloc_msg_buffer(kTestLargeMsgSize);

  rpc_->enqueue_response(reinterpret_cast<ReqHandle *>(sslot_0),
                         &sslot_0->dyn_resp_msgbuf_);
  ASSERT_EQ(sslot_0->server_info_.sav_num_req_pkts_, k_num_req_pkts);

  pkthdr_tx_queue_->pop();  // Remove the response packet

  // The request-for-response packet that is recevied
  pkthdr_t rfr;
  rfr.format(kTestReqType, 0 /* msg_size */, server.session_num_, PktType::kRFR,
             k_num_req_pkts /* pkt_num */, kSessionReqWindow);

  // Receive RFR for an old request (past)
  // Expect: It's dropped
  sslot_0->cur_req_num_ += kSessionReqWindow;
  rpc_->process_rfr_st(sslot_0, &rfr);
  ASSERT_EQ(sslot_0->server_info_.num_rx_, k_num_req_pkts);
  ASSERT_TRUE(pkthdr_tx_queue_->size() == 0);
  sslot_0->cur_req_num_ -= kSessionReqWindow;

  // Receive an in-order RFR (in-order)
  // Expect: Response packet #1 is sent
  rpc_->process_rfr_st(sslot_0, &rfr);
  ASSERT_TRUE(pkthdr_tx_queue_->pop().matches(PktType::kResp, k_num_req_pkts));
  ASSERT_EQ(sslot_0->server_info_.num_rx_, k_num_req_pkts + 1);

  // Receive the same RFR again (past)
  // Expect: Response packet is re-sent and TX queue is flushed
  rpc_->process_rfr_st(sslot_0, &rfr);
  ASSERT_TRUE(pkthdr_tx_queue_->pop().matches(PktType::kResp, k_num_req_pkts));
  ASSERT_EQ(sslot_0->server_info_.num_rx_, k_num_req_pkts + 1);
  ASSERT_EQ(rpc_->transport_->testing_.tx_flush_count_, 1);

  // Server should use only the num_rx counter for ordering (sensitivity)
  // Expect: On resetting it, behavior should be exactly like an in-order RFR
  sslot_0->server_info_.num_rx_ = k_num_req_pkts;
  rpc_->process_rfr_st(sslot_0, &rfr);
  ASSERT_TRUE(pkthdr_tx_queue_->pop().matches(PktType::kResp, k_num_req_pkts));
  ASSERT_EQ(sslot_0->server_info_.num_rx_, k_num_req_pkts + 1);
  ASSERT_EQ(rpc_->transport_->testing_.tx_flush_count_, 1);  // Unchanged

  // Receive a future RFR packet for this request (future)
  // Expect: It's dropped
  rfr.pkt_num_ += 2u;
  rpc_->process_rfr_st(sslot_0, &rfr);
  ASSERT_EQ(sslot_0->server_info_.num_rx_, k_num_req_pkts + 1);
  ASSERT_TRUE(pkthdr_tx_queue_->size() == 0);
  rfr.pkt_num_ -= 2u;
}

}  // namespace erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

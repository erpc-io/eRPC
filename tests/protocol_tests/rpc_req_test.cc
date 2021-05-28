#include "protocol_tests.h"

namespace erpc {

TEST_F(RpcTest, process_small_req_st) {
  const auto server = get_local_endpoint();
  const auto client = get_remote_endpoint();
  Session *srv_session = create_server_session_init(client, server);
  SSlot *sslot_0 = &srv_session->sslot_arr_[0];

  // The request packet that is recevied
  uint8_t req[sizeof(pkthdr_t) + kTestSmallMsgSize];
  auto *pkthdr_0 = reinterpret_cast<pkthdr_t *>(req);
  pkthdr_0->format(kTestReqType, kTestSmallMsgSize, server.session_num_,
                   PktType::kReq, 0 /* pkt_num */, kSessionReqWindow);

  // Receive an old request (past)
  // Expect: It's dropped
  sslot_0->cur_req_num_ += 2 * kSessionReqWindow;
  rpc_->process_small_req_st(sslot_0, pkthdr_0);
  ASSERT_EQ(pkthdr_tx_queue_->size(), 0);
  sslot_0->cur_req_num_ -= 2 * kSessionReqWindow;

  // Receive an in-order small request (in-order)
  // Expect: Response handler is called and response is sent
  rpc_->process_small_req_st(sslot_0, pkthdr_0);
  ASSERT_EQ(num_req_handler_calls_, 1);
  ASSERT_EQ(pkthdr_tx_queue_->pop().pkt_type_, PktType::kResp);
  num_req_handler_calls_ = 0;

  // Receive the same request again (past)
  // Expect: Request handler is not called. Resp is re-sent & TX queue flushed.
  rpc_->process_small_req_st(sslot_0, pkthdr_0);
  ASSERT_EQ(num_req_handler_calls_, 0);
  ASSERT_EQ(pkthdr_tx_queue_->pop().pkt_type_, PktType::kResp);
  ASSERT_EQ(rpc_->transport_->testing_.tx_flush_count_, 1);

  // Receive the same request again, but response is not ready (past)
  // Expect: Request handler is not called and response is not re-sent
  MsgBuffer *tx_msgbuf_save = sslot_0->tx_msgbuf_;
  sslot_0->tx_msgbuf_ = nullptr;
  rpc_->process_small_req_st(sslot_0, pkthdr_0);
  ASSERT_EQ(num_req_handler_calls_, 0);
  sslot_0->tx_msgbuf_ = tx_msgbuf_save;

  // Receive the next in-order request (in-order)
  // Expect: Response handler is called and response is sent
  pkthdr_0->req_num_ += kSessionReqWindow;
  rpc_->process_small_req_st(sslot_0, pkthdr_0);
  ASSERT_EQ(num_req_handler_calls_, 1);
  ASSERT_EQ(pkthdr_tx_queue_->pop().pkt_type_, PktType::kResp);
  num_req_handler_calls_ = 0;
}

TEST_F(RpcTest, process_large_req_one_st) {
  const size_t num_pkts_in_req = rpc_->data_size_to_num_pkts(kTestLargeMsgSize);
  ASSERT_GT(num_pkts_in_req, 10);

  const auto server = get_local_endpoint();
  const auto client = get_remote_endpoint();
  Session *srv_session = create_server_session_init(client, server);
  SSlot *sslot_0 = &srv_session->sslot_arr_[0];

  // The request packet that is recevied
  uint8_t req[CTransport::kMTU];
  auto *pkthdr_0 = reinterpret_cast<pkthdr_t *>(req);
  pkthdr_0->format(kTestReqType, kTestLargeMsgSize, server.session_num_,
                   PktType::kReq, 0 /* pkt_num */, kSessionReqWindow);

  // Receive a packet for a past request (past)
  // Expect: It's dropped
  assert(sslot_0->cur_req_num_ == 0);
  sslot_0->cur_req_num_ += 2 * kSessionReqWindow;
  rpc_->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_EQ(pkthdr_tx_queue_->size(), 0);
  ASSERT_EQ(sslot_0->server_info_.num_rx_, 0);
  sslot_0->cur_req_num_ -= 2 * kSessionReqWindow;

  // Receive the zeroth request packet (in-order)
  // Expect: Credit return is sent
  rpc_->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_TRUE(pkthdr_tx_queue_->pop().matches(PktType::kExplCR, 0));
  ASSERT_EQ(sslot_0->server_info_.num_rx_, 1);

  // Receive the next request packet (in-order)
  // Expect: Credit return is sent
  pkthdr_0->pkt_num_++;
  rpc_->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_TRUE(pkthdr_tx_queue_->pop().matches(PktType::kExplCR, 1));
  ASSERT_EQ(sslot_0->server_info_.num_rx_, 2);

  // Receive the same request packet again (past)
  // Expect: Credit return is re-sent and transport is NOT flushed - XXX?
  rpc_->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_TRUE(pkthdr_tx_queue_->pop().matches(PktType::kExplCR, 1));
  ASSERT_EQ(sslot_0->server_info_.num_rx_, 2);
  ASSERT_EQ(rpc_->transport_->testing_.tx_flush_count_, 0);

  // Receive a future packet for this request (future)
  // Expect: It's dropped
  pkthdr_0->pkt_num_ += 2u;
  rpc_->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_EQ(pkthdr_tx_queue_->size(), 0);
  ASSERT_EQ(sslot_0->server_info_.num_rx_, 2);
  pkthdr_0->pkt_num_ -= 2u;

  // Receive the last packet of this request (in-order)
  // Expect: First response packet is sent, and request is buried
  sslot_0->server_info_.num_rx_ = num_pkts_in_req - 1;
  pkthdr_0->pkt_num_ = num_pkts_in_req - 1;
  rpc_->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_TRUE(
      pkthdr_tx_queue_->pop().matches(PktType::kResp, num_pkts_in_req - 1));
  ASSERT_EQ(sslot_0->server_info_.num_rx_, num_pkts_in_req);
  ASSERT_TRUE(sslot_0->server_info_.req_msgbuf_.is_buried());

  // Receive the last request packet again (past)
  // Expect: First response packet is sent and transport flushed
  rpc_->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_TRUE(
      pkthdr_tx_queue_->pop().matches(PktType::kResp, num_pkts_in_req - 1));
  ASSERT_EQ(sslot_0->server_info_.num_rx_, num_pkts_in_req);
  ASSERT_EQ(rpc_->transport_->testing_.tx_flush_count_, 1);
  rpc_->transport_->testing_.tx_flush_count_ = 0;

  // Receive any request packet except the last (past)
  // Expect: Credit return is re-sent and transport is NOT flushed - XXX?
  assert(num_pkts_in_req > 5);
  pkthdr_0->pkt_num_ = 5;
  rpc_->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_TRUE(pkthdr_tx_queue_->pop().matches(PktType::kExplCR, 5));
  ASSERT_EQ(sslot_0->server_info_.num_rx_, num_pkts_in_req);
  ASSERT_EQ(rpc_->transport_->testing_.tx_flush_count_, 0);
}

}  // namespace erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

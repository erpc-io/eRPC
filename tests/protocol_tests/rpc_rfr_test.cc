#include "protocol_tests.h"

namespace erpc {

TEST_F(RpcTest, process_rfr_st) {
  const auto server = get_local_endpoint();
  const auto client = get_remote_endpoint();
  Session *srv_session = create_server_session_init(client, server);
  SSlot *sslot_0 = &srv_session->sslot_arr[0];

  const size_t kNumReqPkts = 5;  // Size of the received request

  // Use enqueue_response() to do much of sslot formatting for the response
  sslot_0->server_info.req_msgbuf =
      rpc->alloc_msg_buffer(kNumReqPkts * (rpc->get_max_data_per_pkt()));
  sslot_0->server_info.num_rx = kNumReqPkts;

  sslot_0->cur_req_num = kSessionReqWindow;
  sslot_0->server_info.req_type = kTestReqType;
  sslot_0->dyn_resp_msgbuf = rpc->alloc_msg_buffer(kTestLargeMsgSize);
  sslot_0->prealloc_used = false;

  rpc->enqueue_response(reinterpret_cast<ReqHandle *>(sslot_0));
  ASSERT_EQ(sslot_0->server_info.sav_num_req_pkts, kNumReqPkts);

  pkthdr_tx_queue->pop();  // Remove the response packet

  // The request-for-response packet that is recevied
  pkthdr_t rfr;
  rfr.format(kTestReqType, 0 /* msg_size */, server.session_num,
             PktType::kPktTypeRFR, kNumReqPkts /* pkt_num */,
             kSessionReqWindow);

  // Receive RFR for an old request (past)
  // Expect: It's dropped
  sslot_0->cur_req_num += kSessionReqWindow;
  rpc->process_rfr_st(sslot_0, &rfr);
  ASSERT_EQ(sslot_0->server_info.num_rx, kNumReqPkts);
  ASSERT_TRUE(pkthdr_tx_queue->size() == 0);
  sslot_0->cur_req_num -= kSessionReqWindow;

  // Receive an in-order RFR (in-order)
  // Expect: Response packet #1 is sent
  rpc->process_rfr_st(sslot_0, &rfr);
  ASSERT_TRUE(
      pkthdr_tx_queue->pop().matches(PktType::kPktTypeResp, kNumReqPkts));
  ASSERT_EQ(sslot_0->server_info.num_rx, kNumReqPkts + 1);

  // Receive the same RFR again (past)
  // Expect: Response packet is re-sent and TX queue is flushed
  rpc->process_rfr_st(sslot_0, &rfr);
  ASSERT_TRUE(
      pkthdr_tx_queue->pop().matches(PktType::kPktTypeResp, kNumReqPkts));
  ASSERT_EQ(sslot_0->server_info.num_rx, kNumReqPkts + 1);
  ASSERT_EQ(rpc->transport->testing.tx_flush_count, 1);

  // Server should use only the num_rx counter for ordering (sensitivity)
  // Expect: On resetting it, behavior should be exactly like an in-order RFR
  sslot_0->server_info.num_rx = kNumReqPkts;
  rpc->process_rfr_st(sslot_0, &rfr);
  ASSERT_TRUE(
      pkthdr_tx_queue->pop().matches(PktType::kPktTypeResp, kNumReqPkts));
  ASSERT_EQ(sslot_0->server_info.num_rx, kNumReqPkts + 1);
  ASSERT_EQ(rpc->transport->testing.tx_flush_count, 1);  // Unchanged

  // Receive a future RFR packet for this request (future)
  // Expect: It's dropped
  rfr.pkt_num += 2u;
  rpc->process_rfr_st(sslot_0, &rfr);
  ASSERT_EQ(sslot_0->server_info.num_rx, kNumReqPkts + 1);
  ASSERT_TRUE(pkthdr_tx_queue->size() == 0);
  rfr.pkt_num -= 2u;
}

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

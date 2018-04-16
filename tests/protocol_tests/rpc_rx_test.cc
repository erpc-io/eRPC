#include "protocol_tests.h"

// Note that only real packet orderings are tested; invalid packet orderings are
// are ignored, and it's OK for eRPC to crash in with such orderings. For
// example, the server cannot receive a future request before it sends a
// response to the current request.
namespace erpc {

static constexpr size_t kTestSmallMsgSize = 32;
static constexpr size_t kTestLargeMsgSize = KB(128);

class TestContext {
 public:
  Rpc<CTransport> *rpc = nullptr;
  size_t num_req_handler_calls = 0;
  size_t num_cont_func_calls = 0;
};

/// The common request handler for subtests. Works for any request size.
/// Copies request to response.
void req_handler(ReqHandle *req_handle, void *_context) {
  auto *context = static_cast<TestContext *>(_context);
  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  const size_t resp_size = req_msgbuf->get_data_size();

  req_handle->dyn_resp_msgbuf = context->rpc->alloc_msg_buffer(resp_size);
  req_handle->prealloc_used = false;
  memcpy(req_handle->dyn_resp_msgbuf.buf, req_msgbuf->buf, resp_size);

  context->rpc->enqueue_response(req_handle);
  context->num_req_handler_calls++;
}

/// The common continuation for subtests.
void cont_func(RespHandle *resp_handle, void *_context, size_t) {
  auto *context = static_cast<TestContext *>(_context);
  context->num_cont_func_calls++;
  context->rpc->release_response(resp_handle);
}

class RpcRxTest : public RpcTest {
 public:
  RpcRxTest() {
    // Set Rpc context
    test_context.rpc = rpc;
    test_context.rpc->set_context(&test_context);
  }

  TestContext test_context;
};

TEST_F(RpcRxTest, process_small_req_st) {
  const auto server = get_local_endpoint();
  const auto client = get_remote_endpoint();
  Session *srv_session = create_server_session_init(client, server);
  SSlot *sslot_0 = &srv_session->sslot_arr[0];

  // The request packet that is recevied
  uint8_t req[sizeof(pkthdr_t) + kTestSmallMsgSize];
  auto *pkthdr_0 = reinterpret_cast<pkthdr_t *>(req);
  pkthdr_0->format(kTestReqType, kTestSmallMsgSize, server.session_num,
                   PktType::kPktTypeReq, 0 /* pkt_num */, kSessionReqWindow);

  // Receive an old request (past)
  // Expect: It's dropped
  sslot_0->cur_req_num += 2 * kSessionReqWindow;
  rpc->process_small_req_st(sslot_0, pkthdr_0);
  ASSERT_EQ(pkthdr_tx_queue->size(), 0);
  sslot_0->cur_req_num -= 2 * kSessionReqWindow;

  // Receive an in-order small request (in-order)
  // Expect: Response handler is called and response is sent
  rpc->process_small_req_st(sslot_0, pkthdr_0);
  ASSERT_EQ(test_context.num_req_handler_calls, 1);
  ASSERT_EQ(pkthdr_tx_queue->pop().pkt_type, PktType::kPktTypeResp);
  test_context.num_req_handler_calls = 0;

  // Receive the same request again (past)
  // Expect: Request handler is not called. Resp is re-sent & TX queue flushed.
  rpc->process_small_req_st(sslot_0, pkthdr_0);
  ASSERT_EQ(test_context.num_req_handler_calls, 0);
  ASSERT_EQ(pkthdr_tx_queue->pop().pkt_type, PktType::kPktTypeResp);
  ASSERT_EQ(rpc->transport->testing.tx_flush_count, 1);

  // Receive the same request again, but response is not ready (past)
  // Expect: Request handler is not called and response is not re-sent
  MsgBuffer *tx_msgbuf_save = sslot_0->tx_msgbuf;
  sslot_0->tx_msgbuf = nullptr;
  rpc->process_small_req_st(sslot_0, pkthdr_0);
  ASSERT_EQ(test_context.num_req_handler_calls, 0);
  sslot_0->tx_msgbuf = tx_msgbuf_save;

  // Receive the next in-order request (in-order)
  // Expect: Response handler is called and response is sent
  pkthdr_0->req_num += kSessionReqWindow;
  rpc->process_small_req_st(sslot_0, pkthdr_0);
  ASSERT_EQ(test_context.num_req_handler_calls, 1);
  ASSERT_EQ(pkthdr_tx_queue->pop().pkt_type, PktType::kPktTypeResp);
  test_context.num_req_handler_calls = 0;
}

TEST_F(RpcRxTest, process_small_resp_st) {
  const auto client = get_local_endpoint();
  const auto server = get_remote_endpoint();
  Session *clt_session = create_client_session_connected(client, server);
  SSlot *sslot_0 = &clt_session->sslot_arr[0];

  MsgBuffer req = rpc->alloc_msg_buffer(kTestSmallMsgSize);
  MsgBuffer local_resp = rpc->alloc_msg_buffer(kTestSmallMsgSize);

  // Use enqueue_request() to do sslot formatting for the request. Small request
  // is sent right away, so it uses credits.
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
  rpc->process_small_resp_st(sslot_0, pkthdr_0, batch_rx_tsc);
  ASSERT_EQ(sslot_0->client_info.num_rx, 0);
  ASSERT_EQ(test_context.num_cont_func_calls, 0);
  sslot_0->cur_req_num -= kSessionReqWindow;

  // Receive resp while request progress is rolled back (roll-back)
  // Expect: It's dropped.
  assert(sslot_0->client_info.num_tx == 1);
  sslot_0->client_info.num_tx = 0;
  rpc->process_small_resp_st(sslot_0, pkthdr_0, batch_rx_tsc);
  ASSERT_EQ(test_context.num_cont_func_calls, 0);
  sslot_0->client_info.num_tx = 1;

  // Receive an in-order small response (in-order)
  // Expect: Continuation is invoked.
  rpc->process_small_resp_st(sslot_0, pkthdr_0, batch_rx_tsc);
  ASSERT_EQ(test_context.num_cont_func_calls, 1);
  ASSERT_EQ(sslot_0->tx_msgbuf, nullptr);  // Response received
  test_context.num_cont_func_calls = 0;

  // Receive the same response again (past)
  // Expect: It's dropped
  rpc->process_small_resp_st(sslot_0, pkthdr_0, batch_rx_tsc);
  ASSERT_EQ(test_context.num_cont_func_calls, 0);
}

TEST_F(RpcRxTest, process_expl_cr_st) {
  const auto client = get_local_endpoint();
  const auto server = get_remote_endpoint();
  Session *clt_session = create_client_session_connected(client, server);
  SSlot *sslot_0 = &clt_session->sslot_arr[0];

  // Ensure that we have enough packets to fill one credit window
  static_assert(kTestLargeMsgSize / CTransport::kMTU > kSessionCredits, "");

  MsgBuffer req = rpc->alloc_msg_buffer(kTestLargeMsgSize);
  MsgBuffer resp = rpc->alloc_msg_buffer(kTestSmallMsgSize);  // Unused

  // Use enqueue_request() to do sslot formatting for the request. This should
  // use all credits since the message is large.
  rpc->enqueue_request(0, kTestReqType, &req, &resp, cont_func, kTestTag);
  assert(sslot_0->client_info.num_tx == kSessionCredits);
  assert(clt_session->client_info.credits == 0);

  // Construct the basic explicit credit return packet
  pkthdr_t expl_cr;
  expl_cr.format(kTestReqType, 0 /* msg_size */, client.session_num,
                 PktType::kPktTypeExplCR, 0 /* pkt_num */, kSessionReqWindow);

  size_t batch_rx_tsc = rdtsc();  // Stress batch TSC use

  // Receive credit return for an old request (past)
  // Expect: It's dropped
  sslot_0->cur_req_num += kSessionReqWindow;
  rpc->process_expl_cr_st(sslot_0, &expl_cr, batch_rx_tsc);
  ASSERT_EQ(sslot_0->client_info.num_rx, 0);
  sslot_0->cur_req_num -= kSessionReqWindow;

  // Receive an in-order explicit credit return (in-order)
  // Expect: num_rx and credits are bumped
  rpc->process_expl_cr_st(sslot_0, &expl_cr, batch_rx_tsc);
  ASSERT_EQ(sslot_0->client_info.num_rx, 1);
  ASSERT_EQ(clt_session->client_info.credits, 1);

  // Receive the same explicit credit return again (past)
  // Expect: It's dropped
  rpc->process_expl_cr_st(sslot_0, &expl_cr, batch_rx_tsc);
  ASSERT_EQ(sslot_0->client_info.num_rx, 1);

  // Client should use only the num_rx counter for ordering (sensitivity)
  // Expect: On resetting it, behavior should be like an in-order explicit CR
  sslot_0->client_info.num_rx--;
  clt_session->client_info.credits--;
  rpc->process_expl_cr_st(sslot_0, &expl_cr, batch_rx_tsc);
  ASSERT_EQ(sslot_0->client_info.num_rx, 1);
  ASSERT_EQ(clt_session->client_info.credits, 1);

  // Receive explicit credit return for a future pkt in this request (roll-back)
  // Expect: It's dropped
  sslot_0->client_info.num_rx = 1;  // Roll-back
  expl_cr.pkt_num = 2;              // Future
  rpc->process_expl_cr_st(sslot_0, &expl_cr, batch_rx_tsc);
  ASSERT_EQ(sslot_0->client_info.num_rx, 1);
  expl_cr.pkt_num = 0;
}

TEST_F(RpcRxTest, process_req_for_resp_st) {
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
             PktType::kPktTypeReqForResp, kNumReqPkts /* pkt_num */,
             kSessionReqWindow);

  // Receive RFR for an old request (past)
  // Expect: It's dropped
  sslot_0->cur_req_num += kSessionReqWindow;
  rpc->process_req_for_resp_st(sslot_0, &rfr);
  ASSERT_EQ(sslot_0->server_info.num_rx, kNumReqPkts);
  ASSERT_TRUE(pkthdr_tx_queue->size() == 0);
  sslot_0->cur_req_num -= kSessionReqWindow;

  // Receive an in-order RFR (in-order)
  // Expect: Response packet #1 is sent
  rpc->process_req_for_resp_st(sslot_0, &rfr);
  ASSERT_TRUE(
      pkthdr_tx_queue->pop().matches(PktType::kPktTypeResp, kNumReqPkts));
  ASSERT_EQ(sslot_0->server_info.num_rx, kNumReqPkts + 1);

  // Receive the same RFR again (past)
  // Expect: Response packet is re-sent and TX queue is flushed
  rpc->process_req_for_resp_st(sslot_0, &rfr);
  ASSERT_TRUE(
      pkthdr_tx_queue->pop().matches(PktType::kPktTypeResp, kNumReqPkts));
  ASSERT_EQ(sslot_0->server_info.num_rx, kNumReqPkts + 1);
  ASSERT_EQ(rpc->transport->testing.tx_flush_count, 1);

  // Server should use only the num_rx counter for ordering (sensitivity)
  // Expect: On resetting it, behavior should be exactly like an in-order RFR
  sslot_0->server_info.num_rx = kNumReqPkts;
  rpc->process_req_for_resp_st(sslot_0, &rfr);
  ASSERT_TRUE(
      pkthdr_tx_queue->pop().matches(PktType::kPktTypeResp, kNumReqPkts));
  ASSERT_EQ(sslot_0->server_info.num_rx, kNumReqPkts + 1);
  ASSERT_EQ(rpc->transport->testing.tx_flush_count, 1);  // Unchanged

  // Receive a future RFR packet for this request (future)
  // Expect: It's dropped
  rfr.pkt_num += 2u;
  rpc->process_req_for_resp_st(sslot_0, &rfr);
  ASSERT_EQ(sslot_0->server_info.num_rx, kNumReqPkts + 1);
  ASSERT_TRUE(pkthdr_tx_queue->size() == 0);
  rfr.pkt_num -= 2u;
}

TEST_F(RpcRxTest, process_large_req_one_st) {
  const size_t num_pkts_in_req = rpc->data_size_to_num_pkts(kTestLargeMsgSize);
  ASSERT_GT(num_pkts_in_req, 10);

  const auto server = get_local_endpoint();
  const auto client = get_remote_endpoint();
  Session *srv_session = create_server_session_init(client, server);
  SSlot *sslot_0 = &srv_session->sslot_arr[0];

  // The request packet that is recevied
  uint8_t req[CTransport::kMTU];
  auto *pkthdr_0 = reinterpret_cast<pkthdr_t *>(req);
  pkthdr_0->format(kTestReqType, kTestLargeMsgSize, server.session_num,
                   PktType::kPktTypeReq, 0 /* pkt_num */, kSessionReqWindow);

  // Receive a packet for a past request (past)
  // Expect: It's dropped
  assert(sslot_0->cur_req_num == 0);
  sslot_0->cur_req_num += 2 * kSessionReqWindow;
  rpc->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_EQ(pkthdr_tx_queue->size(), 0);
  ASSERT_EQ(sslot_0->server_info.num_rx, 0);
  sslot_0->cur_req_num -= 2 * kSessionReqWindow;

  // Receive the zeroth request packet (in-order)
  // Expect: Credit return is sent
  rpc->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_TRUE(pkthdr_tx_queue->pop().matches(PktType::kPktTypeExplCR, 0));
  ASSERT_EQ(sslot_0->server_info.num_rx, 1);

  // Receive the next request packet (in-order)
  // Expect: Credit return is sent
  pkthdr_0->pkt_num++;
  rpc->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_TRUE(pkthdr_tx_queue->pop().matches(PktType::kPktTypeExplCR, 1));
  ASSERT_EQ(sslot_0->server_info.num_rx, 2);

  // Receive the same request packet again (past)
  // Expect: Credit return is re-sent and transport is NOT flushed - XXX?
  rpc->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_TRUE(pkthdr_tx_queue->pop().matches(PktType::kPktTypeExplCR, 1));
  ASSERT_EQ(sslot_0->server_info.num_rx, 2);
  ASSERT_EQ(rpc->transport->testing.tx_flush_count, 0);

  // Receive a future packet for this request (future)
  // Expect: It's dropped
  pkthdr_0->pkt_num += 2u;
  rpc->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_EQ(pkthdr_tx_queue->size(), 0);
  ASSERT_EQ(sslot_0->server_info.num_rx, 2);
  pkthdr_0->pkt_num -= 2u;

  // Receive the last packet of this request (in-order)
  // Expect: First response packet is sent, and request is buried
  sslot_0->server_info.num_rx = num_pkts_in_req - 1;
  pkthdr_0->pkt_num = num_pkts_in_req - 1;
  rpc->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_TRUE(pkthdr_tx_queue->pop().matches(PktType::kPktTypeResp,
                                             num_pkts_in_req - 1));
  ASSERT_EQ(sslot_0->server_info.num_rx, num_pkts_in_req);
  ASSERT_TRUE(sslot_0->server_info.req_msgbuf.is_buried());

  // Receive the last request packet again (past)
  // Expect: First response packet is sent and transport flushed
  rpc->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_TRUE(pkthdr_tx_queue->pop().matches(PktType::kPktTypeResp,
                                             num_pkts_in_req - 1));
  ASSERT_EQ(sslot_0->server_info.num_rx, num_pkts_in_req);
  ASSERT_EQ(rpc->transport->testing.tx_flush_count, 1);
  rpc->transport->testing.tx_flush_count = 0;

  // Receive any request packet except the last (past)
  // Expect: Credit return is re-sent and transport is NOT flushed - XXX?
  assert(num_pkts_in_req > 5);
  pkthdr_0->pkt_num = 5;
  rpc->process_large_req_one_st(sslot_0, pkthdr_0);
  ASSERT_TRUE(pkthdr_tx_queue->pop().matches(PktType::kPktTypeExplCR, 5));
  ASSERT_EQ(sslot_0->server_info.num_rx, num_pkts_in_req);
  ASSERT_EQ(rpc->transport->testing.tx_flush_count, 0);
}

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

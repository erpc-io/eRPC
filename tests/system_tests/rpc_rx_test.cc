#include "system_tests.h"

namespace erpc {

static constexpr size_t kTestSmallMsgSize = 32;
static constexpr size_t kTestLargeMsgSize = MB(1);

class TestContext {
 public:
  Rpc<TestTransport> *rpc = nullptr;
  size_t num_req_handler_calls = 0;
  size_t num_cont_func_calls = 0;
};

/// The common request handler for subtests. Copies request to response.
void req_handler(ReqHandle *req_handle, void *_context) {
  auto *context = static_cast<TestContext *>(_context);
  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  const size_t resp_size = req_msgbuf->get_data_size();

  req_handle->prealloc_used = true;
  context->rpc->resize_msg_buffer(&req_handle->pre_resp_msgbuf, resp_size);
  memcpy(req_handle->pre_resp_msgbuf.buf, req_msgbuf->buf, resp_size);

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

//
// process_small_req_st()
//
TEST_F(RpcRxTest, process_small_req_st) {
  const auto server = get_local_endpoint();
  const auto client = get_remote_endpoint();
  Session *srv_session = create_server_session_init(client, server);
  SSlot *sslot = &srv_session->sslot_arr[0];

  // The request packet that is recevied
  MsgBuffer req = rpc->alloc_msg_buffer(kTestSmallMsgSize);
  pkthdr_t *pkthdr_0 = req.get_pkthdr_0();
  pkthdr_0->req_type = kTestReqType;
  pkthdr_0->msg_size = kTestSmallMsgSize;
  pkthdr_0->dest_session_num = server.session_num;
  pkthdr_0->pkt_type = PktType::kPktTypeReq;
  pkthdr_0->pkt_num = 0;
  pkthdr_0->req_num = Session::kSessionReqWindow;

  // In-order: Receive an in-order small request.
  // Response handler is called and response is sent.
  rpc->process_small_req_st(sslot, pkthdr_0);
  ASSERT_EQ(test_context.num_req_handler_calls, 1);
  ASSERT_EQ(rpc->testing.pkthdr_tx_queue.pop().pkt_type, PktType::kPktTypeResp);
  test_context.num_req_handler_calls = 0;

  // Duplicate: Receive the same request again.
  // Request handler is not called. Response is re-sent, and TX queue flushed.
  rpc->process_small_req_st(sslot, pkthdr_0);
  ASSERT_EQ(test_context.num_req_handler_calls, 0);
  ASSERT_EQ(rpc->testing.pkthdr_tx_queue.pop().pkt_type, PktType::kPktTypeResp);
  ASSERT_EQ(rpc->transport->testing.tx_flush_count, 1);

  // Duplicate: Receive the same request again, but response is not ready yet.
  // Request handler is not called and response is not re-sent.
  MsgBuffer *tx_msgbuf_save = sslot->tx_msgbuf;
  sslot->tx_msgbuf = nullptr;
  rpc->process_small_req_st(sslot, pkthdr_0);
  ASSERT_EQ(test_context.num_req_handler_calls, 0);
  sslot->tx_msgbuf = tx_msgbuf_save;

  // Past: Receive an old request.
  // Request handler is not called and response is not re-sent.
  sslot->cur_req_num += Session::kSessionReqWindow;
  rpc->process_small_req_st(sslot, pkthdr_0);
  ASSERT_EQ(test_context.num_req_handler_calls, 0);
  ASSERT_EQ(rpc->testing.pkthdr_tx_queue.size(), 0);
  sslot->cur_req_num -= Session::kSessionReqWindow;

  // In-order: Receive the next in-order request.
  // Response handler is called and response is sent.
  pkthdr_0->req_num += Session::kSessionReqWindow;
  rpc->process_small_req_st(sslot, pkthdr_0);
  ASSERT_EQ(test_context.num_req_handler_calls, 1);
  ASSERT_EQ(rpc->testing.pkthdr_tx_queue.pop().pkt_type, PktType::kPktTypeResp);
  test_context.num_req_handler_calls = 0;

  // Future: Receive a future request packet. This is an error.
  pkthdr_0->req_num += 2 * Session::kSessionReqWindow;
  ASSERT_DEATH(rpc->process_small_req_st(sslot, pkthdr_0), ".*");
}

//
// process_small_resp_st()
//
TEST_F(RpcRxTest, process_small_resp_st) {
  const auto client = get_local_endpoint();
  const auto server = get_remote_endpoint();
  Session *clt_session = create_client_session_connected(client, server);
  SSlot *sslot_0 = &clt_session->sslot_arr[0];

  MsgBuffer req = rpc->alloc_msg_buffer(kTestSmallMsgSize);
  MsgBuffer local_resp = rpc->alloc_msg_buffer(kTestSmallMsgSize);

  // Use enqueue_request() to do sslot formatting for the request. Small request
  // is sent right away, so it uses credits.
  rpc->enqueue_request(0, kTestReqType, &req, &local_resp, cont_func, 0);
  assert(clt_session->client_info.credits == Session::kSessionCredits - 1);

  // Construct the basic test response packet
  MsgBuffer remote_resp = rpc->alloc_msg_buffer(kTestSmallMsgSize);
  pkthdr_t *pkthdr_0 = remote_resp.get_pkthdr_0();
  pkthdr_0->req_type = kTestReqType;
  pkthdr_0->msg_size = kTestSmallMsgSize;
  pkthdr_0->dest_session_num = client.session_num;
  pkthdr_0->pkt_type = PktType::kPktTypeResp;
  pkthdr_0->pkt_num = 0;
  pkthdr_0->req_num = Session::kSessionReqWindow;

  // Roll-back: Receive resp while request progress is rolled back.
  // Response is ignored.
  assert(sslot_0->client_info.req_sent == 1);
  sslot_0->client_info.req_sent = 0;
  rpc->process_small_resp_st(sslot_0, pkthdr_0);
  ASSERT_EQ(test_context.num_cont_func_calls, 0);
  sslot_0->client_info.req_sent = 1;

  // In-order: Receive an in-order small response.
  // Continuation is invoked.
  rpc->process_small_resp_st(sslot_0, pkthdr_0);
  ASSERT_EQ(test_context.num_cont_func_calls, 1);
  ASSERT_EQ(sslot_0->tx_msgbuf, nullptr);  // Response received
  test_context.num_cont_func_calls = 0;

  // Duplicate: Receive the same response again.
  // It's ignored.
  rpc->process_small_resp_st(sslot_0, pkthdr_0);
  ASSERT_EQ(test_context.num_cont_func_calls, 0);

  // Past: Receive an old response.
  // It's ignored.
  sslot_0->cur_req_num += Session::kSessionReqWindow;
  rpc->process_small_resp_st(sslot_0, pkthdr_0);
  ASSERT_EQ(test_context.num_cont_func_calls, 0);
  sslot_0->cur_req_num -= Session::kSessionReqWindow;

  // Future: Receive a future response packet.
  // This is an error.
  pkthdr_0->req_num += Session::kSessionReqWindow;
  ASSERT_DEATH(rpc->process_small_resp_st(sslot_0, pkthdr_0), ".*");
}

//
// process_expl_cr_st()
//
TEST_F(RpcRxTest, process_expl_cr_st) {
  const auto client = get_local_endpoint();
  const auto server = get_remote_endpoint();
  Session *clt_session = create_client_session_connected(client, server);
  SSlot *sslot_0 = &clt_session->sslot_arr[0];

  MsgBuffer req = rpc->alloc_msg_buffer(kTestLargeMsgSize);
  MsgBuffer resp = rpc->alloc_msg_buffer(kTestSmallMsgSize);  // Unused

  // Use enqueue_request() to do sslot formatting for the request. Large request
  // is queued, so it doesn't use credits for now.
  rpc->enqueue_request(0, kTestReqType, &req, &resp, cont_func, 0);
  assert(clt_session->client_info.credits == Session::kSessionCredits);

  // Construct the basic explicit credit return packet
  pkthdr_t expl_cr;
  expl_cr.req_type = kTestReqType;
  expl_cr.msg_size = 0;
  expl_cr.dest_session_num = client.session_num;
  expl_cr.pkt_type = PktType::kPktTypeExplCR;
  expl_cr.pkt_num = 0;
  expl_cr.req_num = Session::kSessionReqWindow;

  // Past: Receive credit return for an old request.
  // It's ignored.
  sslot_0->cur_req_num += Session::kSessionReqWindow;
  rpc->process_expl_cr_st(sslot_0, &expl_cr);
  ASSERT_EQ(sslot_0->client_info.expl_cr_rcvd, 0);
  sslot_0->cur_req_num -= Session::kSessionReqWindow;

  // In-order: Receive an in-order explicit credit return.
  // This bumps sslot's expl_cr_rcvd
  sslot_0->client_info.req_sent = 1;
  clt_session->client_info.credits = Session::kSessionCredits - 1;
  rpc->process_expl_cr_st(sslot_0, &expl_cr);
  ASSERT_EQ(sslot_0->client_info.expl_cr_rcvd, 1);

  // Duplicate: Receive the same explicit credit return again.
  // It's ignored.
  rpc->process_expl_cr_st(sslot_0, &expl_cr);
  ASSERT_EQ(sslot_0->client_info.expl_cr_rcvd, 1);

  // Roll-back: Receive explicit credit return for a future pkt in this request.
  // It's ignored.
  expl_cr.pkt_num = 1;
  rpc->process_expl_cr_st(sslot_0, &expl_cr);
  ASSERT_EQ(sslot_0->client_info.expl_cr_rcvd, 1);
  expl_cr.pkt_num = 0;

  // Future: Receive explicit credit return for a future request.
  // This is an error.
  expl_cr.req_num += Session::kSessionReqWindow;
  ASSERT_DEATH(rpc->process_expl_cr_st(sslot_0, &expl_cr), ".*");
}

//
// process_req_for_resp_st()
//
TEST_F(RpcRxTest, process_req_for_resp_st) {
  const auto server = get_local_endpoint();
  const auto client = get_remote_endpoint();
  Session *srv_session = create_server_session_init(client, server);
  SSlot *sslot_0 = &srv_session->sslot_arr[0];

  // Use enqueue_response() to do much of sslot formatting for the response.
  sslot_0->cur_req_num = Session::kSessionReqWindow;
  sslot_0->server_info.req_type = kTestReqType;
  sslot_0->dyn_resp_msgbuf = rpc->alloc_msg_buffer(kTestLargeMsgSize);
  sslot_0->prealloc_used = false;
  rpc->enqueue_response(reinterpret_cast<ReqHandle *>(sslot_0));
  rpc->testing.pkthdr_tx_queue.pop();  // Remove the response packet

  // The request-for-response packet that is recevied
  pkthdr_t rfr;
  rfr.req_type = kTestReqType;
  rfr.msg_size = 0;
  rfr.dest_session_num = server.session_num;
  rfr.pkt_type = PktType::kPktTypeReqForResp;
  rfr.pkt_num = 1;
  rfr.req_num = Session::kSessionReqWindow;

  // Past: Receive RFR for an old request.
  // It's dropped.
  sslot_0->cur_req_num += Session::kSessionReqWindow;
  rpc->process_req_for_resp_st(sslot_0, &rfr);
  ASSERT_EQ(sslot_0->server_info.rfr_rcvd, 0);
  ASSERT_TRUE(rpc->testing.pkthdr_tx_queue.size() == 0);
  sslot_0->cur_req_num -= Session::kSessionReqWindow;

  // In-order: Receive an in-order RFR.
  // Response packet #1 is sent.
  rpc->process_req_for_resp_st(sslot_0, &rfr);
  pkthdr_t resp = rpc->testing.pkthdr_tx_queue.pop();
  ASSERT_EQ(resp.pkt_type, PktType::kPktTypeResp);
  ASSERT_EQ(resp.pkt_num, 1);
  ASSERT_EQ(sslot_0->server_info.rfr_rcvd, 1);

  // Duplicate: Receive the same RFR again.
  // Response packet is re-sent and TX queue is flushed.
  rpc->process_req_for_resp_st(sslot_0, &rfr);
  resp = rpc->testing.pkthdr_tx_queue.pop();
  ASSERT_EQ(resp.pkt_type, PktType::kPktTypeResp);
  ASSERT_EQ(resp.pkt_num, 1);
  ASSERT_EQ(sslot_0->server_info.rfr_rcvd, 1);
  ASSERT_EQ(rpc->transport->testing.tx_flush_count, 1);

  // Sensitivity: Server should use only the rfr_rcvd counter for ordering.
  // On resetting it, behavior should be exactly like an in-order RFR.
  sslot_0->server_info.rfr_rcvd = 0;
  rpc->process_req_for_resp_st(sslot_0, &rfr);
  resp = rpc->testing.pkthdr_tx_queue.pop();
  ASSERT_EQ(resp.pkt_type, PktType::kPktTypeResp);
  ASSERT_EQ(resp.pkt_num, 1);
  ASSERT_EQ(sslot_0->server_info.rfr_rcvd, 1);

  // Future: Receive a future RFR packet for this request.
  // It's dropped.
  rfr.pkt_num += 2u;
  rpc->process_req_for_resp_st(sslot_0, &rfr);
  ASSERT_EQ(sslot_0->server_info.rfr_rcvd, 1);
  ASSERT_TRUE(rpc->testing.pkthdr_tx_queue.size() == 0);
  rfr.pkt_num -= 2u;

  // Future: Receive an RFR packet for a future request.
  // This is an error.
  rfr.req_num += Session::kSessionReqWindow;
  ASSERT_DEATH(rpc->process_req_for_resp_st(sslot_0, &rfr), ".*");
}

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

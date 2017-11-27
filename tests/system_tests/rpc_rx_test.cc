#include "system_tests.h"

namespace erpc {

static constexpr size_t kTestSmallMsgSize = 32;

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

  create_server_session_init(client, server);
  Session *srv_session = rpc->session_vec[0];
  rpc->transport->resolve_remote_routing_info(
      &srv_session->client.routing_info);

  // The request packet that is recevied
  MsgBuffer req = rpc->alloc_msg_buffer(kTestSmallMsgSize);
  pkthdr_t *pkthdr_0 = req.get_pkthdr_0();
  pkthdr_0->req_type = kTestReqType;
  pkthdr_0->msg_size = kTestSmallMsgSize;
  pkthdr_0->dest_session_num = server.session_num;
  pkthdr_0->pkt_type = kPktTypeReq;
  pkthdr_0->pkt_num = 0;
  pkthdr_0->req_num = Session::kSessionReqWindow;

  // Receive an in-order small request packet.
  // Response handler is called and response is sent.
  rpc->process_small_req_st(&srv_session->sslot_arr[0],
                            reinterpret_cast<uint8_t *>(pkthdr_0));
  ASSERT_EQ(test_context.num_req_handler_calls, 1);
  ASSERT_EQ(rpc->testing.pkthdr_tx_queue.pop().pkt_type, PktType::kPktTypeResp);
  test_context.num_req_handler_calls = 0;

  // Receive the same request packet again.
  // Request handler is not called. Response is re-sent, and TX queue flushed.
  rpc->process_small_req_st(&srv_session->sslot_arr[0],
                            reinterpret_cast<uint8_t *>(pkthdr_0));
  ASSERT_EQ(test_context.num_req_handler_calls, 0);
  ASSERT_EQ(rpc->testing.pkthdr_tx_queue.pop().pkt_type, PktType::kPktTypeResp);
  ASSERT_EQ(rpc->transport->testing.tx_flush_count, 1);

  // Advance the sslot's cur_req_num, and receive the request packet again.
  // Request handler is not called and response is not re-sent.
  rpc->session_vec[0]->sslot_arr[0].cur_req_num += Session::kSessionReqWindow;
  rpc->process_small_req_st(&srv_session->sslot_arr[0],
                            reinterpret_cast<uint8_t *>(pkthdr_0));
  ASSERT_EQ(test_context.num_req_handler_calls, 0);
  ASSERT_EQ(rpc->testing.pkthdr_tx_queue.size(), 0);
  rpc->session_vec[0]->sslot_arr[0].cur_req_num -= Session::kSessionReqWindow;

  // Mark the response as unavailable, and receive the request packet again.
  // Request handler is not called and response is not re-sent.
  MsgBuffer *tx_msgbuf_save = rpc->session_vec[0]->sslot_arr[0].tx_msgbuf;
  rpc->session_vec[0]->sslot_arr[0].tx_msgbuf = nullptr;
  rpc->process_small_req_st(&srv_session->sslot_arr[0],
                            reinterpret_cast<uint8_t *>(pkthdr_0));
  ASSERT_EQ(test_context.num_req_handler_calls, 0);
  rpc->session_vec[0]->sslot_arr[0].tx_msgbuf = tx_msgbuf_save;

  // Receive the next in-order small request packet.
  // Response handler is called and response is sent.
  pkthdr_0->req_num += Session::kSessionReqWindow;
  rpc->process_small_req_st(&srv_session->sslot_arr[0],
                            reinterpret_cast<uint8_t *>(pkthdr_0));
  ASSERT_EQ(test_context.num_req_handler_calls, 1);
  ASSERT_EQ(rpc->testing.pkthdr_tx_queue.pop().pkt_type, PktType::kPktTypeResp);
  test_context.num_req_handler_calls = 0;
}

//
// process_small_resp_st()
//
TEST_F(RpcRxTest, process_small_resp_st) {
  const auto client = get_local_endpoint();
  const auto server = get_remote_endpoint();

  create_client_session_init(client, server);
  Session *clt_session = rpc->session_vec[0];
  clt_session->server.session_num = server.session_num;
  rpc->transport->resolve_remote_routing_info(
      &clt_session->server.routing_info);
  clt_session->state = SessionState::kConnected;

  // Use enqueue_request() to do sslot formatting for the request
  MsgBuffer req = rpc->alloc_msg_buffer(kTestSmallMsgSize);
  MsgBuffer local_resp = rpc->alloc_msg_buffer(kTestSmallMsgSize);
  rpc->enqueue_request(0, kTestReqType, &req, &local_resp, cont_func, 0);

  SSlot &sslot_0 = clt_session->sslot_arr[0];
  ASSERT_NE(sslot_0.tx_msgbuf, nullptr);  // Response not received

  // Construct the test response packet received
  MsgBuffer remote_resp = rpc->alloc_msg_buffer(kTestSmallMsgSize);
  pkthdr_t *pkthdr_0 = remote_resp.get_pkthdr_0();
  pkthdr_0->req_type = kTestReqType;
  pkthdr_0->msg_size = kTestSmallMsgSize;
  pkthdr_0->dest_session_num = client.session_num;
  pkthdr_0->pkt_type = kPktTypeResp;
  pkthdr_0->pkt_num = 0;
  pkthdr_0->req_num = Session::kSessionReqWindow;

  // Receive an in-order small response packet.
  // Continuation is invoked.
  rpc->process_small_resp_st(&clt_session->sslot_arr[0],
                             reinterpret_cast<uint8_t *>(pkthdr_0));
  ASSERT_EQ(sslot_0.tx_msgbuf, nullptr);  // Response received
  ASSERT_EQ(test_context.num_cont_func_calls, 1);
  ASSERT_EQ(local_resp.get_data_size(), kTestSmallMsgSize);
  test_context.num_cont_func_calls = 0;

  // Receive the packet again.
  // Continution is not invoked.
  rpc->process_small_resp_st(&clt_session->sslot_arr[0],
                             reinterpret_cast<uint8_t *>(pkthdr_0));
  ASSERT_EQ(test_context.num_cont_func_calls, 0);
  test_context.num_cont_func_calls = 0;
}

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

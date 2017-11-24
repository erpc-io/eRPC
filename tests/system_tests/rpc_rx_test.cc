#include "system_tests.h"

namespace erpc {

static constexpr size_t kTestSmallReqSize = 32;

class TestContext {
 public:
  Rpc<TestTransport> *rpc = nullptr;
  size_t num_resps = 0;
};

/// The common request handler for subtests. Copies request to response.
void req_handler(ReqHandle *req_handle, void *_context) {
  auto *context = static_cast<TestContext *>(_context);
  const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
  const size_t resp_size = req_msgbuf->get_data_size();

  memcpy(req_handle->pre_resp_msgbuf.buf, req_msgbuf->buf, resp_size);
  req_handle->prealloc_used = true;

  context->rpc->enqueue_response(req_handle);
  context->num_resps++;
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

  MsgBuffer req = rpc->alloc_msg_buffer(kTestSmallReqSize);
  strcpy(reinterpret_cast<char *>(req.buf), "req");

  pkthdr_t *pkthdr_0 = req.get_pkthdr_0();
  pkthdr_0->req_type = kTestReqType;
  pkthdr_0->msg_size = kTestSmallReqSize;
  pkthdr_0->dest_session_num = server.session_num;
  pkthdr_0->pkt_type = kPktTypeReq;

  // Process an in-order request
  pkthdr_0->pkt_num = 0;
  pkthdr_0->req_num = Session::kSessionReqWindow;
  rpc->process_small_req_st(&srv_session->sslot_arr[0],
                            reinterpret_cast<uint8_t *>(pkthdr_0));
  ASSERT_EQ(test_context.num_resps, 1);
  test_context.num_resps = 0;

  // Process the same request again. Request handler is not called.
  rpc->process_small_req_st(&srv_session->sslot_arr[0],
                            reinterpret_cast<uint8_t *>(pkthdr_0));
  ASSERT_EQ(test_context.num_resps, 0);
}

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

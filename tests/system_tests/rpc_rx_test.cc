#include "system_tests.h"

namespace erpc {
class RpcRxTest : public RpcTest {
 public:
  static constexpr size_t kTestReqType = 1;

  /// The common request handler for subtests. Copies request to response.
  static void req_handler(ReqHandle *req_handle, void *_context) {
    auto *context = static_cast<TestContext *>(_context);
    const MsgBuffer *req_msgbuf = req_handle->get_req_msgbuf();
    const size_t resp_size = req_msgbuf->get_data_size();

    memcpy(req_handle->pre_resp_msgbuf.buf, req_msgbuf->buf, resp_size);
    req_handle->prealloc_used = true;

    context->rpc->enqueue_response(req_handle);
  }

  RpcRxTest() {
    // Ugh I hate myself
    *const_cast<ReqFunc *>(&rpc->req_func_arr[kTestReqType]) =
        ReqFunc(req_handler, ReqFuncType::kForeground);

    // Set Rpc context
    test_context.rpc = rpc;
    test_context.rpc->set_context(&test_context);
  }

  class TestContext {
   public:
    Rpc<TestTransport> *rpc = nullptr;
    size_t num_resps = 0;
  };

  TestContext test_context;
};

//
// process_comps_st()
//
TEST_F(RpcRxTest, process_small_req_st) {
  const auto server = get_local_endpoint();
  const auto client = get_remote_endpoint();
  const size_t req_size = 32;

  create_server_session_init(client, server);
  Session *srv_session = rpc->session_vec[0];

  MsgBuffer req = rpc->alloc_msg_buffer(req_size);
  rt_assert(req.buf != nullptr, "Request alloc failed");

  strcpy(reinterpret_cast<char *>(req.buf), "req");
  pkthdr_t *pkthdr_0 = req.get_pkthdr_0();
  pkthdr_0->req_type = kTestReqType;
  pkthdr_0->msg_size = req_size;
  pkthdr_0->dest_session_num = server.session_num;
  pkthdr_0->pkt_type = kPktTypeReq;
  pkthdr_0->pkt_num = 0;
  pkthdr_0->req_num = Session::kSessionReqWindow;

  rpc->process_small_req_st(&srv_session->sslot_arr[0],
                            reinterpret_cast<uint8_t *>(pkthdr_0));
}

}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

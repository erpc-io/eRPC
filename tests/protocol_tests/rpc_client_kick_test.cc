#include "protocol_tests.h"

namespace erpc {

// Ensure that we have enough packets to fill one credit window
static_assert(kTestLargeMsgSize / CTransport::kMTU > kSessionCredits, "");

/// Common setup code for client kick tests
class RpcClientKickTest : public RpcTest {
 public:
  SessionEndpoint client, server;
  Session *clt_session;
  SSlot *sslot_0;
  MsgBuffer req, resp;

  RpcClientKickTest() {
    client = get_local_endpoint();
    server = get_remote_endpoint();
    clt_session = create_client_session_connected(client, server);
    sslot_0 = &clt_session->sslot_arr[0];

    req = rpc->alloc_msg_buffer(kTestLargeMsgSize);
    resp = rpc->alloc_msg_buffer(kTestSmallMsgSize);  // Unused
  }
};

TEST_F(RpcClientKickTest, client_kick_st_no_credits) {
  // Use enqueue_request() to do sslot formatting. This uses all credits.
  rpc->enqueue_request(0, kTestReqType, &req, &resp, cont_func, kTestTag);
  assert(clt_session->client_info.credits == 0);

  ASSERT_DEATH(rpc->client_kick_st(sslot_0), ".*");
}
}  // End erpc

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

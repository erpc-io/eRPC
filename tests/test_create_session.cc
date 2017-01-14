#include <gtest/gtest.h>
#include <map>
#include <thread>
#include "rpc.h"

using namespace ERpc;

#define NEXUS_UDP_PORT 31851
#define SERVER_APP_TID 1
#define CLIENT_APP_TID 2

struct test_context_t {
  bool is_client;
  test_context_t(bool is_client) : is_client(is_client) {}
};

void test_sm_hander(Session *session, SessionMgmtEventType sm_event_type,
                    SessionMgmtErrType sm_err_type, void *_context) {
  _unused(session);
  _unused(sm_event_type);
  _unused(sm_err_type);
  _unused(_context);
}

TEST(create_session, create_session) {
  Nexus nexus(NEXUS_UDP_PORT);

  /* Create the client Rpc */
  test_context_t *client_context = new test_context_t(true);
  std::vector<size_t> port_vec = {0};
  Rpc<InfiniBandTransport> rpc(&nexus, (void *)client_context, CLIENT_APP_TID,
                               &test_sm_hander, port_vec);

  /* Test: Successful creation */
  Session *session_1 = rpc.create_session(port_vec[0], "akalia-cmudesk",
                                          SERVER_APP_TID, port_vec[0]);
  ASSERT_TRUE(session_1 != nullptr);
  ASSERT_TRUE(session_1->client.session_num == 0);

  /* Test: Invalid local fabric port */
  Session *session_2 = rpc.create_session(port_vec[0] + 1, "akalia-cmudesk",
                                          SERVER_APP_TID, port_vec[0]);
  ASSERT_TRUE(session_2 == nullptr);

  /* Test: Invalid remote fabric port */
  Session *session_3 = rpc.create_session(port_vec[0] + 1, "akalia-cmudesk",
                                          SERVER_APP_TID, kMaxFabDevPorts);
  ASSERT_TRUE(session_3 == nullptr);

  /* Test: Try to create session to self */
  Session *session_4 = rpc.create_session(port_vec[0], "akalia-cmudesk",
                                          CLIENT_APP_TID, port_vec[0]);
  ASSERT_TRUE(session_4 == nullptr);

  /* Test: Try to create another session to the same remote Rpc. */
  Session *session_5 = rpc.create_session(port_vec[0], "akalia-cmudesk",
                                          SERVER_APP_TID, port_vec[0]);
  ASSERT_TRUE(session_5 == nullptr);

  /* Test: Use the same remote hostname but a different app TID */
  Session *session_6 = rpc.create_session(port_vec[0], "akalia-cmudesk",
                                          SERVER_APP_TID + 2, port_vec[0]);
  ASSERT_TRUE(session_6 != nullptr);
  ASSERT_TRUE(session_6->client.session_num == 1);

  /* Test: Use a different hostname hostname but the same app TID */
  Session *session_7 = rpc.create_session(port_vec[0], "akalia-cmudesk-2",
                                          SERVER_APP_TID, port_vec[0]);
  ASSERT_TRUE(session_7 != nullptr);
  ASSERT_TRUE(session_7->client.session_num == 2);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

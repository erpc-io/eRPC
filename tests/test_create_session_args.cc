#include <gtest/gtest.h>
#include <atomic>
#include <map>
#include <thread>
#include "rpc.h"

using namespace ERpc;

#define NEXUS_UDP_PORT 31851
#define EVENT_LOOP_MS 200

#define SERVER_APP_TID 100
#define CLIENT_APP_TID 200

/* Shared between client and server thread */
std::atomic<size_t> server_count;
std::vector<size_t> port_vec = {0};

void test_sm_hander(Session *session, SessionMgmtEventType sm_event_type,
                    SessionMgmtErrType sm_err_type, void *_context) {
  _unused(session);
  _unused(sm_event_type);
  _unused(sm_err_type);
  _unused(_context);
}

/* The client thread */
void client_thread_func(Nexus *nexus) {
  /* Start the tests only after all servers are ready */
  while (server_count != 1) {
    usleep(1);
  }

  /* Create the Rpc */
  Rpc<InfiniBandTransport> rpc(nexus, (void *)nullptr, CLIENT_APP_TID,
                               &test_sm_hander, port_vec);

  {
    /* Test: Correct args */
    Session *session = rpc.create_session(port_vec[0], "akalia-cmudesk",
                                          SERVER_APP_TID, port_vec[0]);
    ASSERT_TRUE(session != nullptr);
  }

  {
    /* Test: Unmanaged local port */
    Session *session = rpc.create_session(port_vec[0] + 1, "akalia-cmudesk",
                                          SERVER_APP_TID, port_vec[0]);
    ASSERT_TRUE(session == nullptr);
  }

  {
    /* Test: Unmanaged remote port */
    Session *session = rpc.create_session(port_vec[0] + 1, "akalia-cmudesk",
                                          SERVER_APP_TID, kMaxFabDevPorts);
    ASSERT_TRUE(session == nullptr);
  }

  {
    /* Test: Try to create session to self */
    Session *session = rpc.create_session(port_vec[0], "akalia-cmudesk",
                                          CLIENT_APP_TID, port_vec[0]);
    ASSERT_TRUE(session == nullptr);
  }

  {
    /* Test: Try to create another session to the same remote Rpc. */
    Session *session = rpc.create_session(port_vec[0], "akalia-cmudesk",
                                          SERVER_APP_TID, port_vec[0]);
    ASSERT_TRUE(session == nullptr);
  }
}

/* The server thread */
void server_thread_func(Nexus *nexus, size_t app_tid) {
  Rpc<InfiniBandTransport> rpc(nexus, nullptr, app_tid, &test_sm_hander,
                               port_vec);

  server_count++;
  rpc.run_event_loop_timeout(EVENT_LOOP_MS);
}

TEST(test_build, test_build) {
  Nexus nexus(NEXUS_UDP_PORT);

  /* Launch the server thread */
  std::thread server_thread(server_thread_func, &nexus, SERVER_APP_TID);

  /* Launch the client thread */
  std::thread client_thread(client_thread_func, &nexus);

  server_thread.join();
  client_thread.join();
}

int main(int argc, char **argv) {
  server_count = 0;
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

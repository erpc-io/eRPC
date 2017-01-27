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
std::atomic<bool> server_ready;
const uint8_t phy_port = 0;
const size_t numa_node = 0;
char local_hostname[kMaxHostnameLen];

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
  while (!server_ready) {
    usleep(1);
  }

  /* Create the Rpc */
  Rpc<InfiniBandTransport> rpc(nexus, (void *)nullptr, CLIENT_APP_TID,
                               &test_sm_hander, phy_port, numa_node);

  {
    /* Test: Correct args */
    Session *session =
        rpc.create_session(local_hostname, SERVER_APP_TID, phy_port);
    ASSERT_TRUE(session != nullptr);
  }

  {
    /* Test: Invalid remote port, which can be detected locally */
    Session *session =
        rpc.create_session(local_hostname, SERVER_APP_TID, kMaxPhyPorts);
    ASSERT_TRUE(session == nullptr);
  }

  {
    /* Test: Try to create session to self */
    Session *session =
        rpc.create_session(local_hostname, CLIENT_APP_TID, phy_port);
    ASSERT_TRUE(session == nullptr);
  }

  {
    /* Test: Try to create another session to the same remote Rpc. */
    Session *session =
        rpc.create_session(local_hostname, SERVER_APP_TID, phy_port);
    ASSERT_TRUE(session == nullptr);
  }
}

/* The server thread */
void server_thread_func(Nexus *nexus, uint8_t app_tid) {
  Rpc<InfiniBandTransport> rpc(nexus, nullptr, app_tid, &test_sm_hander,
                               phy_port, numa_node);

  server_ready = true;
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
  Nexus::get_hostname(local_hostname);
  server_ready = false;
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

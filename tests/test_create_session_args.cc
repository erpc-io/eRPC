#include <gtest/gtest.h>
#include <atomic>
#include <map>
#include <thread>
#include "rpc.h"

using namespace ERpc;

#define NEXUS_UDP_PORT 31851
#define EVENT_LOOP_MS 200

#define SERVER_RPC_ID 100
#define CLIENT_RPC_ID 200

// Shared between client and server thread
std::atomic<bool> server_ready;
const uint8_t phy_port = 0;
const size_t numa_node = 0;
char local_hostname[kMaxHostnameLen];

/// The session managament handler that is never invoked
void test_sm_handler(int session_num, SmEventType sm_event_type,
                     SmErrType sm_err_type, void *_context) {
  _unused(session_num);
  _unused(sm_event_type);
  _unused(sm_err_type);
  _unused(_context);
}

// The client thread
void client_thread_func(Nexus<IBTransport> *nexus) {
  // Start the tests only after the server is ready
  while (!server_ready) {
    usleep(1);
  }

  // Create the Rpc
  Rpc<IBTransport> rpc(nexus, nullptr, CLIENT_RPC_ID, &test_sm_handler,
                       phy_port, numa_node);

  {
    // Test: Correct args
    int session_num =
        rpc.create_session(local_hostname, SERVER_RPC_ID, phy_port);
    ASSERT_GE(session_num, 0);
  }

  {
    // Test: Invalid remote port, which can be detected locally
    int session_num =
        rpc.create_session(local_hostname, SERVER_RPC_ID, kMaxPhyPorts);
    ASSERT_LT(session_num, 0);
  }

  {
    // Test: Try to create session to self
    int session_num =
        rpc.create_session(local_hostname, CLIENT_RPC_ID, phy_port);
    ASSERT_LT(session_num, 0);
  }

  {
    // Test: Try to create another session to the same remote Rpc
    int session_num =
        rpc.create_session(local_hostname, SERVER_RPC_ID, phy_port);
    ASSERT_LT(session_num, 0);
  }
}

// The server thread
void server_thread_func(Nexus<IBTransport> *nexus, uint8_t rpc_id) {
  Rpc<IBTransport> rpc(nexus, nullptr, rpc_id, &test_sm_handler, phy_port,
                       numa_node);

  server_ready = true;
  rpc.run_event_loop_timeout(EVENT_LOOP_MS);
}

/// Test: Check if passing invalid arguments to create_session gives an error
TEST(TestBuild, TestBuild) {
  Nexus<IBTransport> nexus(NEXUS_UDP_PORT, 0);  // 0 background threads

  // Launch the server thread
  std::thread server_thread(server_thread_func, &nexus, SERVER_RPC_ID);

  // Launch the client thread
  std::thread client_thread(client_thread_func, &nexus);

  server_thread.join();
  client_thread.join();
}

int main(int argc, char **argv) {
  Nexus<IBTransport>::get_hostname(local_hostname);
  server_ready = false;
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

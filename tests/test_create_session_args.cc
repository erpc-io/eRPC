#include <gtest/gtest.h>
#include <atomic>
#include <map>
#include <thread>
#include "rpc.h"
#include "test_basics.h"

// Shared between client and server thread
std::atomic<bool> server_ready;

// The client thread
void client_thread_func(Nexus *nexus) {
  // Start the tests only after the server is ready
  while (!server_ready) {
    usleep(1);
  }

  // Create the Rpc
  erpc::Rpc<erpc::IBTransport> rpc(nexus, nullptr, kAppClientRpcId,
                                   &basic_sm_handler, kAppPhyPort,
                                   kAppNumaNode);

  {
    // Test: Correct args
    int session_num =
        rpc.create_session("localhost", kAppServerRpcId, kAppPhyPort);
    ASSERT_GE(session_num, 0);
  }

  {
    // Test: Invalid remote port, which can be detected locally
    int session_num =
        rpc.create_session("localhost", kAppServerRpcId, kMaxPhyPorts);
    ASSERT_LT(session_num, 0);
  }

  {
    // Test: Try to create session to self
    int session_num =
        rpc.create_session("localhost", kAppClientRpcId, kAppPhyPort);
    ASSERT_LT(session_num, 0);
  }
}

// The server thread
void server_thread_func(Nexus *nexus, uint8_t rpc_id) {
  erpc::Rpc<erpc::IBTransport> rpc(nexus, nullptr, rpc_id, &basic_sm_handler,
                                   kAppPhyPort, kAppNumaNode);

  server_ready = true;
  rpc.run_event_loop(kAppEventLoopMs);
}

/// Test: Check if passing invalid arguments to create_session gives an error
TEST(TestBuild, TestBuild) {
  Nexus nexus("localhost", kAppNexusUdpPort, 0);  // 0 bg threads

  // Launch the server thread
  std::thread server_thread(server_thread_func, &nexus, kAppServerRpcId);

  // Launch the client thread
  std::thread client_thread(client_thread_func, &nexus);

  server_thread.join();
  client_thread.join();
}

int main(int argc, char **argv) {
  server_ready = false;
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

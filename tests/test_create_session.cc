#include <gtest/gtest.h>
#include <atomic>
#include <map>
#include <thread>
#include "rpc.h"

using namespace ERpc;

#define NEXUS_UDP_PORT 31851
#define EVENT_LOOP_MS 2000

#define SERVER_APP_TID 100
#define CLIENT_APP_TID 200

/* Shared between client and server thread */
std::atomic<size_t> server_count;
const uint8_t phy_port = 0;
const size_t numa_node = 0;
char local_hostname[kMaxHostnameLen];

struct client_context_t {
  size_t nb_sm_events;
  SessionMgmtErrType exp_err;

  client_context_t() { nb_sm_events = 0; }
};

/* Only invoked for clients */
void test_sm_hander(Session *session, SessionMgmtEventType sm_event_type,
                    SessionMgmtErrType sm_err_type, void *_context) {
  ASSERT_TRUE(_context != nullptr);
  client_context_t *context = (client_context_t *)_context;
  context->nb_sm_events++;

  /* Check that the error type matches the expected value */
  ASSERT_EQ(sm_err_type, context->exp_err);

  /* If the error type is really an error, the event should be connect failed */
  if (sm_err_type == SessionMgmtErrType::kNoError) {
    ASSERT_EQ(session->state, SessionState::kConnected);
    ASSERT_EQ(sm_event_type, SessionMgmtEventType::kConnected);
  } else {
    ASSERT_EQ(session->state, SessionState::kError);
    ASSERT_EQ(sm_event_type, SessionMgmtEventType::kConnectFailed);
  }
}

/* The server thread used by all tests */
void server_thread_func(Nexus *nexus, uint8_t app_tid) {
  Rpc<InfiniBandTransport> rpc(nexus, nullptr, app_tid, &test_sm_hander,
                               phy_port, numa_node);

  server_count++;
  rpc.run_event_loop_timeout(EVENT_LOOP_MS);
}

//
// Successful connection establishment
//
void simple_connect(Nexus *nexus) {
  while (server_count != 1) { /* Wait for server */
    usleep(1);
  }

  auto *client_context = new client_context_t();
  Rpc<InfiniBandTransport> rpc(nexus, (void *)client_context, CLIENT_APP_TID,
                               &test_sm_hander, phy_port, numa_node);

  /* Connect the session */
  client_context->exp_err = SessionMgmtErrType::kNoError;
  Session *session =
      rpc.create_session(local_hostname, SERVER_APP_TID, phy_port);
  ASSERT_TRUE(session != nullptr);

  rpc.run_event_loop_timeout(EVENT_LOOP_MS);
  ASSERT_EQ(client_context->nb_sm_events, 1);
}

TEST(SuccessfulConnect, SuccessfulConnect) {
  Nexus nexus(NEXUS_UDP_PORT, .8);
  server_count = 0;

  std::thread server_thread(server_thread_func, &nexus, SERVER_APP_TID);
  std::thread client_thread(simple_connect, &nexus);
  server_thread.join();
  client_thread.join();
}

//
// Create (and connect) a session with an invalid remote port. The server should
// reply with the error code
//
void invalid_remote_port(Nexus *nexus) {
  while (server_count != 1) { /* Wait for server */
    usleep(1);
  }

  auto *client_context = new client_context_t();
  Rpc<InfiniBandTransport> rpc(nexus, (void *)client_context, CLIENT_APP_TID,
                               &test_sm_hander, phy_port, numa_node);

  /* Connect the session */
  client_context->exp_err = SessionMgmtErrType::kInvalidRemotePort;
  Session *session =
      rpc.create_session(local_hostname, SERVER_APP_TID, phy_port + 1);
  ASSERT_TRUE(session != nullptr);

  rpc.run_event_loop_timeout(EVENT_LOOP_MS);
  ASSERT_EQ(client_context->nb_sm_events, 1);
}

TEST(InvalidRemotePort, InvalidRemotePort) {
  Nexus nexus(NEXUS_UDP_PORT, .8);
  server_count = 0;

  std::thread server_thread(server_thread_func, &nexus, SERVER_APP_TID);
  std::thread client_thread(invalid_remote_port, &nexus);
  server_thread.join();
  client_thread.join();
}

int main(int argc, char **argv) {
  Nexus::get_hostname(local_hostname);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

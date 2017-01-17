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

void server_thread_func(Nexus *nexus, size_t app_tid);

/* Shared between client and server thread */
std::atomic<bool> server_ready; /* Client starts after server is ready */
std::atomic<bool> client_done; /* Server ends after client is done */

std::vector<size_t> port_vec = {0};

struct client_context_t {
  size_t nb_sm_events;
  SessionMgmtErrType exp_err;

  client_context_t() {
    nb_sm_events = 0;
  }
};

void test_sm_hander(Session *session, SessionMgmtEventType sm_event_type,
                    SessionMgmtErrType sm_err_type, void *_context) {
  _unused(session);
  _unused(sm_event_type);

  client_context_t *context = (client_context_t *)_context;
  context->nb_sm_events++;

  /* Check that the error type matches the expected value */
  ASSERT_EQ(sm_err_type, context->exp_err);
}

void simple_disconnect(Nexus *nexus) {
  while (!server_ready) { /* Wait for server */
    usleep(1);
  }

  auto *client_context = new client_context_t();
  Rpc<InfiniBandTransport> rpc(nexus, (void *)client_context, CLIENT_APP_TID,
                               &test_sm_hander, port_vec);

  /* Connect the session */
  client_context->exp_err = SessionMgmtErrType::kNoError;
  Session *session = rpc.create_session(port_vec[0], "akalia-cmudesk",
                                        SERVER_APP_TID, port_vec[0]);
  rpc.run_event_loop_timeout(EVENT_LOOP_MS);

  ASSERT_EQ(client_context->nb_sm_events, 1); /* The connect event */
  ASSERT_EQ(session->state, SessionState::kConnected);

  /* Disconnect the session */
  client_context->exp_err = SessionMgmtErrType::kNoError;
  rpc.destroy_session(session);
  rpc.run_event_loop_timeout(EVENT_LOOP_MS);

  ASSERT_EQ(client_context->nb_sm_events, 2); /* The disconnect event */

  client_done = true;
}

TEST(SimpleDisconnect, SimpleDisconnect) {
  Nexus nexus(NEXUS_UDP_PORT, .8);
  server_ready = false;
  client_done = false;

  std::thread server_thread(server_thread_func, &nexus, SERVER_APP_TID);
  std::thread client_thread(simple_disconnect, &nexus);
  server_thread.join();
  client_thread.join();
}

/* The server thread used by all tests */
void server_thread_func(Nexus *nexus, size_t app_tid) {
  Rpc<InfiniBandTransport> rpc(nexus, nullptr, app_tid, &test_sm_hander,
                               port_vec);

  server_ready = true;

  while (!client_done) { /* Wait for the client */
    rpc.run_event_loop_timeout(EVENT_LOOP_MS);
  }
}


int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#include <gtest/gtest.h>
#include <atomic>
#include <map>
#include <thread>
#include "rpc.h"

using namespace ERpc;

#define NUM_CLIENT_SESSIONS 8 /* Number of sessions the client creates */
#define NEXUS_UDP_PORT 31851
#define EVENT_LOOP_MS 2000

#define SERVER_APP_TID 100
#define CLIENT_APP_TID 200
static_assert(CLIENT_APP_TID > SERVER_APP_TID + NUM_CLIENT_SESSIONS, "");

/* Shared between client and server thread */
std::atomic<size_t> server_count;
std::vector<size_t> port_vec = {0};

struct client_context_t {
  size_t nb_sm_events;
  SessionMgmtErrType err_map[NUM_CLIENT_SESSIONS];

  client_context_t() {
    nb_sm_events = 0;
    /* Initialize the error map */
    for (int i = 0; i < NUM_CLIENT_SESSIONS; i++) {
      err_map[i] =
          static_cast<SessionMgmtErrType>(std::numeric_limits<int>::max());
    }
  }
};

void test_sm_hander(Session *session, SessionMgmtEventType sm_event_type,
                    SessionMgmtErrType sm_err_type, void *_context) {
  ASSERT_TRUE(_context != nullptr);
  client_context_t *context = (client_context_t *)_context;
  context->nb_sm_events++;

  /* Check that the error type matches the expected value */
  size_t client_session_num = session->client.session_num;
  ASSERT_EQ(sm_err_type, context->err_map[client_session_num]);

  /* If the error type is really an error, the event should be connect failed */
  if (sm_err_type != SessionMgmtErrType::kNoError) {
    ASSERT_EQ(sm_event_type, SessionMgmtEventType::kConnectFailed);
  } else {
    ASSERT_EQ(sm_event_type, SessionMgmtEventType::kConnected);
  }
}

/* The client thread */
void client_thread_func(Nexus *nexus) {
  /* Start the tests only after all servers are ready */
  while (server_count != NUM_CLIENT_SESSIONS) {
    usleep(1);
  }

  /* Use a different remote TID for each session up to NUM_CLIENT_SESSIONS. */
  size_t rem_app_tid = SERVER_APP_TID;

  /* Create the Rpc */
  client_context_t *client_context = new client_context_t();
  Rpc<InfiniBandTransport> rpc(nexus, (void *)client_context, CLIENT_APP_TID,
                               &test_sm_hander, port_vec);

  auto &err_map = client_context->err_map;

  //
  // Tests that actually generate a connect request
  //

  {
    /* Test: Successful connection */
    Session *session = rpc.create_session(port_vec[0], "akalia-cmudesk",
                                          rem_app_tid++, port_vec[0]);
    ASSERT_TRUE(session != nullptr);
    ASSERT_EQ(session->client.session_num, 0);
    err_map[session->client.session_num] = SessionMgmtErrType::kNoError;
  }

  {
    /* Test: Unmanaged remote port */
    Session *session = rpc.create_session(port_vec[0], "akalia-cmudesk",
                                          rem_app_tid++, port_vec[0] + 1);
    ASSERT_TRUE(session != nullptr);
    ASSERT_EQ(session->client.session_num, 1);
    err_map[session->client.session_num] =
        SessionMgmtErrType::kInvalidRemotePort;
  }

  rpc.run_event_loop_timeout(EVENT_LOOP_MS);

  /* Check that we actually received the expected number of response packets */
  ASSERT_EQ(client_context->nb_sm_events, 2);

  //
  // Tests that don't generate a connect request because of invalid params
  //

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

  /*
   * Launch the server threads. Bind all server threads to core 0 to avoid
   * overload as the event loops will use 100% CPU.
   */
  std::thread server_threads[NUM_CLIENT_SESSIONS];
  for (size_t i = 0; i < NUM_CLIENT_SESSIONS; i++) {
    server_threads[i] =
        std::thread(server_thread_func, &nexus, SERVER_APP_TID + i);

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    int rc = pthread_setaffinity_np(server_threads[i].native_handle(),
                                    sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
      fprintf(stderr, "Error calling pthread_setaffinity_np.\n");
    }
  }

  /* Launch the client thread */
  std::thread client_thread(client_thread_func, &nexus);

  for (size_t i = 0; i < NUM_CLIENT_SESSIONS; i++) {
    server_threads[i].join();
  }

  client_thread.join();
}

int main(int argc, char **argv) {
  server_count = 0;
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

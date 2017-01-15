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
  SessionMgmtErrType err_map[NUM_CLIENT_SESSIONS];

  client_context_t() {
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
  /* Use a different remote TID for each session up to NUM_CLIENT_SESSIONS. */
  size_t rem_app_tid = SERVER_APP_TID;

  /* Create the Rpc */
  client_context_t *client_context = new client_context_t();
  auto &err_map = client_context->err_map;

  Rpc<InfiniBandTransport> rpc(nexus, (void *)client_context, CLIENT_APP_TID,
                               &test_sm_hander, port_vec);

  /* Test: Successful connection */
  Session *session_1 = rpc.create_session(port_vec[0], "akalia-cmudesk",
                                          rem_app_tid++, port_vec[0]);
  ASSERT_TRUE(session_1 != nullptr);
  ASSERT_EQ(session_1->client.session_num, 0);
  err_map[session_1->client.session_num] = SessionMgmtErrType::kNoError;

  /* Test: Invalid remote port */
  Session *session_2 = rpc.create_session(port_vec[0], "akalia-cmudesk",
                                          rem_app_tid++, port_vec[0] + 1);
  ASSERT_TRUE(session_2 != nullptr);
  ASSERT_EQ(session_2->client.session_num, 1);
  err_map[session_2->client.session_num] =
      SessionMgmtErrType::kInvalidRemotePort;

  /* Send the connect requests only after the server is ready */
  while (server_count != NUM_CLIENT_SESSIONS) {
    usleep(1);
  }

  /* Initiate the connect request for session 1 */
  bool connect_1 = rpc.connect_session(session_1);
  ASSERT_TRUE(connect_1);

  /* Try to initiate the connect request for session 1 again */
  connect_1 = rpc.connect_session(session_1);
  ASSERT_FALSE(connect_1);

  /* Initiate the connect request for session 2 */
  bool connect_2 = rpc.connect_session(session_2);
  ASSERT_TRUE(connect_2);

  rpc.run_event_loop_timeout(EVENT_LOOP_MS);
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
   * overload as the event loop will use 100% CPU.
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

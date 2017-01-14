#include <gtest/gtest.h>
#include <map>
#include <thread>
#include "rpc.h"

using namespace ERpc;

#define MAX_CLIENT_SESSIONS 1000 /* Max number of tests */
#define NEXUS_UDP_PORT 31851
#define SERVER_APP_TID 1
#define CLIENT_APP_TID 2
#define EVENT_LOOP_MS 2000

/* Shared between client and server thread */
volatile int server_ready;
std::vector<int> port_vec = {0};

/* Used to map client session numbers to the expected error codes */
SessionMgmtErrType err_map[MAX_CLIENT_SESSIONS];

struct test_context_t {
  bool is_client;
  test_context_t(bool is_client) : is_client(is_client) {}
};

void test_sm_hander(Session *session, SessionMgmtEventType sm_event_type,
                    SessionMgmtErrType sm_err_type, void *_context) {
  assert(sm_err_type == SessionMgmtErrType::kNoError);
  test_context_t *context = (test_context_t *)_context;

  printf("Thread %s received event of type %s on session %p, context = %p\n",
         context->is_client ? "client" : "server",
         session_mgmt_event_type_str(sm_event_type).c_str(), session, context);
}

/* The client thread */
void client_thread_func(Nexus *nexus) {
  /* Initialize the error map */
  for (int i = 0; i < MAX_CLIENT_SESSIONS; i++) {
    err_map[i] =
        static_cast<SessionMgmtErrType>(std::numeric_limits<int>::max());
  }

  /* Create the Rpc */
  test_context_t *client_context = new test_context_t(true);
  Rpc<InfiniBandTransport> rpc(nexus, (void *)client_context, CLIENT_APP_TID,
                               &test_sm_hander, port_vec);

  /* #1: Successful connection */
  Session *session_1 = rpc.create_session(port_vec[0], "akalia-cmudesk",
                                          SERVER_APP_TID, port_vec[0]);
  ASSERT_TRUE(session_1 != nullptr);
  ASSERT_TRUE(session_1->client.session_num == 0);
  err_map[session_1->client.session_num] = SessionMgmtErrType::kNoError;

  /* Send the connect requests only after the server is ready */
  while (server_ready == 0) {
    usleep(1);
  }

  rpc.connect_session(session_1);
  rpc.run_event_loop_timeout(EVENT_LOOP_MS);
}

/* The server thread */
void server_thread_func(Nexus *nexus) {
  test_context_t *server_context = new test_context_t(false);

  Rpc<InfiniBandTransport> rpc(nexus, (void *)server_context, SERVER_APP_TID,
                               &test_sm_hander, port_vec);

  server_ready = 1;
  rpc.run_event_loop_timeout(EVENT_LOOP_MS);
}

TEST(test_build, test_build) {
  server_ready = 0;
  Nexus nexus(NEXUS_UDP_PORT);

  std::thread client_thread(client_thread_func, &nexus);
  std::thread server_thread(server_thread_func, &nexus);

  client_thread.join();
  server_thread.join();
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

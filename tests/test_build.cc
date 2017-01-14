#include <gtest/gtest.h>
#include <thread>
#include "rpc.h"

using namespace ERpc;

#define NEXUS_UDP_PORT 31851
#define SERVER_APP_TID 1
#define CLIENT_APP_TID 2

volatile int server_ready = 0;
std::vector<int> port_vec = {0};

struct test_context_t {
  std::string thread_name;

  test_context_t(const char *thread_name) : thread_name(thread_name) {}
};

void test_sm_hander(Session *session, SessionMgmtEventType sm_event_type,
                    void *_context) {
  test_context_t *context = (test_context_t *)_context;

  printf("Thread %s received event of type %s on session %p, context = %p\n",
         context->thread_name.c_str(),
         session_mgmt_event_type_str(sm_event_type).c_str(), session, context);
}

void client_thread_func(Nexus *nexus) {
  test_context_t *client_context = new test_context_t("[client_thread]");

  Rpc<InfiniBandTransport> rpc(nexus, (void *)client_context, CLIENT_APP_TID,
                               &test_sm_hander, port_vec);

  Session *session = rpc.create_session(port_vec[0],      /* Local port */
                                        "akalia-cmudesk", /* Remote hostname */
                                        SERVER_APP_TID,   /* Remote app TID */
                                        port_vec[0]);     /* Remote port */

  /* Send the connect request only after the server is ready */
  while (server_ready == 0) {
    usleep(1);
  }

  rpc.connect_session(session);
  rpc.run_event_loop_timeout(5000);

  _unused(session);
}

void server_thread_func(Nexus *nexus) {
  test_context_t *server_context = new test_context_t("[client_thread]");

  Rpc<InfiniBandTransport> rpc(nexus, (void *)server_context, SERVER_APP_TID,
                               &test_sm_hander, port_vec);

  server_ready = 1;
  rpc.run_event_loop_timeout(5000);
}

TEST(test_build, test_build) {
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

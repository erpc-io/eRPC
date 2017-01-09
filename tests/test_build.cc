#include <gtest/gtest.h>
#include "rpc.h"

using namespace ERpc;

void test_sm_hander(Session *session, SessionMgmtEventType sm_event_type,
                    void *context) {
  printf("Received event of type %s on session %p, context = %p\n",
         session_mgmt_event_type_str(sm_event_type).c_str(), session, context);
}

TEST(test_build, test_build) {
  Nexus nexus(31851);
  void *context = nullptr;
  int app_tid = 3;
  std::vector<int> port_vec = {0};

  Rpc<InfiniBandTransport> rpc(&nexus, context, app_tid, &test_sm_hander,
                               port_vec);

  Session *session = rpc.create_session(port_vec[0],  /* Local port */
                                        "localhost",  /* Remote hostname */
                                        app_tid,      /* Remote app TID */
                                        port_vec[0]); /* Remote port */

  rpc.connect_session(session);

  _unused(session);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

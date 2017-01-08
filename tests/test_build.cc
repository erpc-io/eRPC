#include <gtest/gtest.h>
#include "rpc.h"

void session_mgmt_handler(ERpc::Session *session,
                          ERpc::SessionMgmtEventType session_event_type,
                          void *context) {
  _unused(session);
  _unused(session_event_type);
  _unused(context);
}

TEST(test_build, test_build) {
  ERpc::Nexus nexus(31851);
  void *context = nullptr;
  int app_tid = 0;
  std::vector<int> port_vec = {1};

  ERpc::Rpc<ERpc::InfiniBandTransport> rpc(&nexus, context, app_tid,
                                           &session_mgmt_handler, port_vec);

  int a = 1, b = 2;
  assert(a == b);
  ((void)(a));
  ((void)(b));
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

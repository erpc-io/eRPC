#include <gtest/gtest.h>
#include "rpc.h"

TEST(test_build, test_build) {
  ERpc::Nexus nexus(31851);
  std::vector<int> port_vec = {1};

  ERpc::Rpc<ERpc::InfiniBandTransport> rpc(&nexus, 0, port_vec);

  int a = 1, b = 2;
  assert(a == b);
  ((void)(a));
  ((void)(b));
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

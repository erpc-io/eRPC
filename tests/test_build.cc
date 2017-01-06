#include <gtest/gtest.h>
#include "rpc.h"

TEST(test_build, test_build) {
  ERpc::Nexus nexus(31851);
  ERpc::Rpc<ERpc::InfiniBandTransport> rpc(nexus);

  int a = 1, b = 2;
  assert(a == b);
  ((void)(a));
  ((void)(b));
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

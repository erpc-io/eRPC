#include "rpc.h"
#include <gtest/gtest.h>

namespace ERpc {

// Session management tests
class SMTest : public ::testing::Test {
 public:
  SMTest() {}
  ~SMTest() {}
};

TEST_F(SMTest, simpleConnectRequest) {}
}  // End eRPC

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

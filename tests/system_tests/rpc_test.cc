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

#include <gtest/gtest.h>
#include "util/udp_server.h"

TEST(UdpServerTest, Basic) {
  erpc::UDPServer<int> udp_server;
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

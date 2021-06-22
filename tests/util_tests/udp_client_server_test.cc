#include <gtest/gtest.h>
#include "util/udp_client.h"
#include "util/udp_server.h"

TEST(UdpClientServerTest, Basic) {
  erpc::UDPServer<int> udp_server;
  erpc::UDPClient<int> udp_client;
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

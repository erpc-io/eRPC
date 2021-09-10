#include "util/udp_client.h"
#include <gtest/gtest.h>

TEST(UdpClientTest, LocalhostName) {
  erpc::UDPClient<int> udp_client;
  int msg = 33;
  size_t ret = udp_client.send("localhost", 31850, msg);
  ASSERT_EQ(ret, sizeof(int));
}

TEST(UdpClientTest, LocalhostIP) {
  erpc::UDPClient<int> udp_client;
  int msg = 33;
  size_t ret = udp_client.send("127.0.0.1", 31850, msg);
  ASSERT_EQ(ret, sizeof(int));
}

TEST(UdpClientTest, InvalidRemote) {
  erpc::UDPClient<int> udp_client;
  int msg = 33;
  size_t ret = udp_client.send("randomhostnamelikelyinvalid9000", 31850, msg);
  ASSERT_EQ(ret, SIZE_MAX);
}

TEST(UdpClientTest, Basic) { erpc::UDPClient<int> udp_client; }

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

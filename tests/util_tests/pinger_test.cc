#include <gtest/gtest.h>

#define private public
#include "pinger.h"

using namespace erpc;

static double kTestFreqGhz = 2.5;
static double kTestMachineFailureTimeoutMs = 1;

TEST(PingerTest, Client) {
  Pinger pinger(kTestFreqGhz, kTestMachineFailureTimeoutMs);
  pinger.ping_udp_client.enable_recording();

  pinger.unlocked_add_remote_server("server_1");
  usleep(kTestMachineFailureTimeoutMs * 1000);

  std::vector<std::string> failed_servers;
  pinger.do_one(failed_servers);

  // Check the pinger's sent_vec
  assert(failed_servers.size() == 1);
  assert(failed_servers.front() == "server_1");
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

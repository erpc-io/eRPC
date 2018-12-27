#include <gtest/gtest.h>

#define private public
#include "pinger.h"

using namespace erpc;

static double kTestFreqGhz = 2.5;
static double kTestMachineFailureTimeoutMs = 1;

TEST(PingerTest, Base) {
  Pinger pinger(kTestFreqGhz, kTestMachineFailureTimeoutMs);
  pinger.ping_udp_client.enable_recording();

  pinger.unlocked_add_remote_server("server_1");
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

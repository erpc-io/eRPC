#include <gtest/gtest.h>

#define private public
#include "pinger.h"

using namespace erpc;

static double kFreqGhz = 2.5;

TEST(PingerTest, Base) { Pinger pinger(kFreqGhz); }

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

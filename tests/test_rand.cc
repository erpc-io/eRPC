#include <gtest/gtest.h>
#include <limits.h>

#include "test_printf.h"
#include "util/rand.h"

TEST(SlowRandTest, DistributionTest) {
  const size_t iters = 1000000; /* 1 million samples */
  uint64_t max = 0, min = UINT64_MAX;

  ERpc::SlowRand slow_rand;
  double avg = 0;

  for (size_t i = 0; i < iters; i++) {
    uint64_t sample = slow_rand.next_u64();
    avg += sample;
    max = std::max(max, sample);
    min = std::min(min, sample);
  }

  avg /= iters;

  double exp_avg = (UINT64_MAX / 2);
  double percentage_diff = std::fabs(avg - exp_avg) / exp_avg;

  test_printf("Percentage deviation of mean = %.10f (best = 0)\n",
              percentage_diff);
  test_printf("Range coverage = %.10f (best = 1)\n",
              (double)(max - min) / (UINT64_MAX));
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

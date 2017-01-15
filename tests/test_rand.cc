#include <gtest/gtest.h>
#include <limits.h>

#include "test_printf.h"
#include "util/rand.h"

// These tests are basic correctness tests and are not supposed to have any
// statistical meaning.

/**
 * @brief Test if SlowRand is uniform-ish and covers the 64-bit range.
 */
TEST(SlowRandTest, DistributionTest) {
  const size_t iters = 1000000; /* 1 million samples */
  uint64_t max = 0, min = std::numeric_limits<uint64_t>::max();

  ERpc::SlowRand slow_rand;
  double avg = 0;

  for (size_t i = 0; i < iters; i++) {
    uint64_t sample = slow_rand.next_u64();
    avg += sample;
    max = std::max(max, sample);
    min = std::min(min, sample);
  }

  avg /= iters;

  double exp_avg = std::numeric_limits<uint64_t>::max() / 2;
  double fraction_diff = std::fabs(avg - exp_avg) / exp_avg;

  test_printf("Fraction deviation of mean = %.10f (best = 0)\n", fraction_diff);
  test_printf("Range coverage = %.10f (best = 1)\n",
              (double)(max - min) / std::numeric_limits<uint64_t>::max());
}

/**
 * @brief Test if SlowRand mod 100 covers 1--100 with equal-ish probability.
 */
TEST(SlowRandTest, ModHundredTest) {
  const size_t iters = 1000000; /* 1 million samples */
  ERpc::SlowRand slow_rand;

  size_t buckets[100] = {0};

  for (size_t i = 0; i < iters; i++) {
    uint64_t sample = slow_rand.next_u64();
    buckets[sample % 100]++;
  }

  size_t max = 0, min = std::numeric_limits<uint64_t>::max();
  for (size_t i = 0; i < 100; i++) {
    max = std::max(max, buckets[i]);
    min = std::min(min, buckets[i]);
  }

  test_printf("min/max = %.5f (best = 1)\n", (double)min / max);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

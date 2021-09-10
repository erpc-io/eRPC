#include "HdrHistogram_c/src/hdr_histogram.h"
#include <gtest/gtest.h>
#include <util/rand.h>
#include <iostream>

static int64_t kLatencyMinMicroseconds = 1;
static int64_t kLatencyMaxMicroseconds = 1000 * 1000 * 100;  // 100 seconds

TEST(HdrHistogramTest, Basic) {
  hdr_histogram* hist;
  int ret =
      hdr_init(kLatencyMinMicroseconds, kLatencyMaxMicroseconds, 2, &hist);
  EXPECT_EQ(ret, 0);

  size_t hist_memory_sz = hdr_get_memory_size(hist);
  printf("Histogram memory size = %zu bytes\n", hist_memory_sz);
  EXPECT_LE(hist_memory_sz, 1024 * 32);  // 32 KB

  // Check histogram's precision to two digits
  for (size_t i = 0; i < 99; i++)
    hdr_record_value(hist, static_cast<int64_t>(i));
  EXPECT_EQ(hdr_value_at_percentile(hist, 50.0), 49);
  hdr_reset(hist);

  // Few entries
  hdr_record_value(hist, 1);
  hdr_record_value(hist, 2);
  hdr_record_value(hist, 2);
  EXPECT_EQ(hdr_value_at_percentile(hist, 0.0), 1);
  EXPECT_EQ(hdr_value_at_percentile(hist, 33.0), 1);
  EXPECT_EQ(hdr_value_at_percentile(hist, 99.999), 2);
  hdr_reset(hist);

  // A more realistic workload:
  // * A million latency samples between 1 and 32 microseconds
  // * A thousand latency samples around 100 ms
  // * One latency sample at 1 second
  const int64_t k_low_latency = 32;             // 32 microseconds
  const int64_t k_high_latency = (1000 * 100);  // 100 ms
  const int64_t k_max_latency = (1000 * 1000);  // 1 second
  erpc::FastRand fast_rand;
  for (size_t i = 0; i < 1000 * 1000; i++) {
    const int64_t latency_sample = 1 + fast_rand.next_u32() % k_low_latency;
    hdr_record_value(hist, latency_sample);
  }

  for (size_t i = 1; i <= 1000; i++) {
    const size_t latency_sample = k_high_latency + i;  // 100 ms + i us
    hdr_record_value(hist, static_cast<int64_t>(latency_sample));
  }

  hdr_record_value(hist, k_max_latency);

  const int64_t perc_50 = hdr_value_at_percentile(hist, 50);
  const int64_t perc_99 = hdr_value_at_percentile(hist, 99);
  const int64_t perc_998 = hdr_value_at_percentile(hist, 99.8);
  const int64_t perc_9999 = hdr_value_at_percentile(hist, 99.99);
  const int64_t perc_99999 = hdr_value_at_percentile(hist, 99.999);
  const int64_t max_lat = hdr_max(hist);

  printf("50%% 99%% 99.8%% 99.99%% 99.999%% max\n");
  std::cout << perc_50 << " " << perc_99 << " " << perc_998 << " " << perc_9999
            << " " << perc_99999 << " " << max_lat << std::endl;

  EXPECT_LE(perc_50, k_low_latency);
  EXPECT_LE(perc_99, k_low_latency);
  EXPECT_LE(perc_998, k_low_latency);
  EXPECT_GE(perc_9999, k_high_latency);
  EXPECT_GE(perc_99999, k_high_latency);

  // hdr_max() does not give exact max
  EXPECT_LE((max_lat - k_max_latency) * 1.0 / k_max_latency, 0.01);
  EXPECT_NE(max_lat, k_max_latency);

  hdr_close(hist);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

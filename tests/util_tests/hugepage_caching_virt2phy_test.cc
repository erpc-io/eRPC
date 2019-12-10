#include <gtest/gtest.h>
#include <time.h>
#include <algorithm>
#include <vector>
#include "util/huge_alloc.h"
#include "util/test_printf.h"
#include "util/timer.h"
#include "util/virt2phy.h"

using namespace erpc;

// Dummy registration and deregistration functions
Transport::MemRegInfo reg_mr_wrapper(void *, size_t) {
  return Transport::MemRegInfo(nullptr, 0);  // *transport_mr, lkey
}

void dereg_mr_wrapper(Transport::MemRegInfo mr) { _unused(mr); }

/// Test perf
TEST(HugepageCachingVirt2PhyTest, perf) {
  static const size_t kSize = MB(128);
  static const size_t kNumaNode = 0;
  double freq_ghz = measure_rdtsc_freq();

  FastRand fast_rand;
  HugeAlloc huge_alloc(MB(2), kNumaNode, reg_mr_wrapper, dereg_mr_wrapper);

  HugepageCachingVirt2Phy hc_v2p;  // The caching v2p translator
  Buffer buffer = huge_alloc.alloc_raw(kSize, DoRegister::kFalse);
  memset(buffer.buf, 1, kSize);

  size_t num_iters = 1000000;

  size_t start = rdtsc();
  size_t sum = 0;
  for (size_t i = 0; i < num_iters; i++) {
    size_t rand_offset = fast_rand.next_u32() % kSize;
    sum += hc_v2p.translate(&buffer.buf[rand_offset]);
  }

  double ns = to_nsec(rdtsc() - start, freq_ghz);
  printf("%.1f nanoseconds per translation. sum = %zu\n", ns / num_iters, sum);
}

/// Test correctness
TEST(HugepageCachingVirt2PhyTest, Correctness) {
  static const size_t kSize = MB(32);
  static const size_t kNumaNode = 0;

  FastRand fast_rand;

  HugeAlloc huge_alloc(MB(2), kNumaNode, reg_mr_wrapper, dereg_mr_wrapper);

  HugepageCachingVirt2Phy hc_v2p;  // The caching v2p translator
  Virt2Phy v2p;  // A non-caching v2p translator for cross-checking

  Buffer buffer = huge_alloc.alloc_raw(kSize, DoRegister::kFalse);
  memset(buffer.buf, 1, kSize);

  // Test ~ 100 random addresses per hugepage
  size_t num_iters = (kSize / MB(2)) * 100;

  for (size_t i = 0; i < num_iters; i++) {
    size_t rand_offset = fast_rand.next_u32() % kSize;
    ASSERT_EQ(hc_v2p.translate(&buffer.buf[rand_offset]),
              v2p.translate(&buffer.buf[rand_offset]));
  }
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

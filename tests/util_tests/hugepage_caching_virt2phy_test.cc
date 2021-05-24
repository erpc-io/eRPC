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
Transport::mem_reg_info reg_mr_wrapper(void *, size_t) {
  return Transport::mem_reg_info(nullptr, 0);  // *transport_mr, lkey
}

void dereg_mr_wrapper(Transport::mem_reg_info mr) { _unused(mr); }

/// Test perf
TEST(HugepageCachingVirt2PhyTest, perf) {
  static const size_t k_size = MB(128);
  static const size_t k_numa_node = 0;
  double freq_ghz = measure_rdtsc_freq();

  FastRand fast_rand;
  HugeAlloc huge_alloc(MB(2), k_numa_node, reg_mr_wrapper, dereg_mr_wrapper);

  HugepageCachingVirt2Phy hc_v2p;  // The caching v2p translator
  Buffer buffer = huge_alloc.alloc_raw(k_size, DoRegister::kFalse);
  memset(buffer.buf_, 1, k_size);

  size_t num_iters = 1000000;

  size_t start = rdtsc();
  size_t sum = 0;
  for (size_t i = 0; i < num_iters; i++) {
    size_t rand_offset = fast_rand.next_u32() % k_size;
    sum += hc_v2p.translate(&buffer.buf_[rand_offset]);
  }

  double ns = to_nsec(rdtsc() - start, freq_ghz);
  printf("%.1f nanoseconds per translation. sum = %zu\n", ns / num_iters, sum);
}

/// Test correctness
TEST(HugepageCachingVirt2PhyTest, Correctness) {
  static const size_t k_size = MB(32);
  static const size_t k_numa_node = 0;

  FastRand fast_rand;

  HugeAlloc huge_alloc(MB(2), k_numa_node, reg_mr_wrapper, dereg_mr_wrapper);

  HugepageCachingVirt2Phy hc_v2p;  // The caching v2p translator
  Virt2Phy v2p;  // A non-caching v2p translator for cross-checking

  Buffer buffer = huge_alloc.alloc_raw(k_size, DoRegister::kFalse);
  memset(buffer.buf_, 1, k_size);

  // Test ~ 100 random addresses per hugepage
  size_t num_iters = (k_size / MB(2)) * 100;

  for (size_t i = 0; i < num_iters; i++) {
    size_t rand_offset = fast_rand.next_u32() % k_size;
    ASSERT_EQ(hc_v2p.translate(&buffer.buf_[rand_offset]),
              v2p.translate(&buffer.buf_[rand_offset]));
  }
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

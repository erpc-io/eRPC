#include <gtest/gtest.h>
#include <time.h>
#include <algorithm>
#include <vector>
#include "util/huge_alloc.h"
#include "util/test_printf.h"

using namespace erpc;

// Dummy registration and deregistration functions
Transport::MemRegInfo reg_mr_wrapper(void *, size_t) {
  return Transport::MemRegInfo(0, 0);
}

void dereg_mr_wrapper(Transport::MemRegInfo) {}

using namespace std::placeholders;
typename Transport::reg_mr_func_t reg_mr_func =
    std::bind(reg_mr_wrapper, _1, _2);
typename Transport::dereg_mr_func_t dereg_mr_func =
    std::bind(dereg_mr_wrapper, _1);

TEST(TimingWheelTest, 1) {
  // Reserve all memory for high perf
  HugeAlloc *alloc = new HugeAlloc(MB(2), 0, reg_mr_func, dereg_mr_func);

  delete alloc;
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

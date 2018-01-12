#include "cc/timing_wheel.h"
#include <gtest/gtest.h>
#include <time.h>
#include <algorithm>
#include <vector>
#include "util/huge_alloc.h"

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

TEST(TimingWheelTest, Basic) {
  HugeAlloc alloc(MB(2), 0, reg_mr_func, dereg_mr_func);
  TimingWheel wheel(10, &alloc);  // 10 wheel slots
  const size_t dummy_pkt_num = 5;
  std::queue<wheel_ent_t> ret;

  // Empty wheel
  wheel.reap(ret, 0);
  ASSERT_EQ(ret.size(), 0);

  // One entry at position 2
  wheel.insert(2, wheel_ent_t(nullptr, dummy_pkt_num));
  wheel.reap(ret, 2);
  ASSERT_EQ(ret.size(), 1);
  ret.pop();

  // Reap again - this should be empty
  wheel.reap(ret, 2);
  ASSERT_EQ(ret.size(), 0);
}

TEST(TimingWheelTest, Stress) {
  HugeAlloc alloc(MB(2), 0, reg_mr_func, dereg_mr_func);
  TimingWheel wheel(10, &alloc);  // 10 wheel slots
  const size_t dummy_pkt_num = 5;
  std::queue<wheel_ent_t> ret;

  for (size_t iter = 0; iter < 1000; iter++) {
    size_t ws_slot = static_cast<size_t>(rand() % 10);
    size_t num_insertions = 1 + (static_cast<size_t>(rand()) % 10000);

    // Overflow a bucket and check the order of returned entries
    for (size_t ent_i = 0; ent_i < num_insertions; ent_i++) {
      wheel.insert(ws_slot, wheel_ent_t(nullptr, iter + dummy_pkt_num + ent_i));
    }

    wheel.reap(ret, ws_slot);
    ASSERT_EQ(ret.size(), num_insertions);

    for (size_t ent_i = 0; ent_i < num_insertions; ent_i++) {
      const wheel_ent_t &ent = ret.front();
      ASSERT_EQ(ent.pkt_num, iter + dummy_pkt_num + ent_i);
      ret.pop();
    }
  }
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

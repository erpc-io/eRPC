#include <gtest/gtest.h>
#include <time.h>
#include <algorithm>
#include <vector>

#include "util/huge_alloc.h"
#include "util/test_printf.h"

#define private public
#include "cc/timing_wheel.h"

using namespace erpc;

static constexpr size_t kTestMTU = 1024;       // Few wheel slots
static constexpr double kTestWslotWidth = .5;  // .5 microseconds per wheel slot

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
  timing_wheel_args_t args;
  args.mtu = kTestMTU;
  args.freq_ghz = measure_rdtsc_freq();
  args.base_tsc = rdtsc();
  args.wslot_width = kTestWslotWidth;
  args.huge_alloc = &alloc;

  TimingWheel wheel(args);
  const auto dummy_ent = wheel_ent_t(nullptr, 1);

  // Empty wheel
  wheel.reap(args.base_tsc);
  ASSERT_EQ(wheel.ready_queue.size(), 0);

  // One entry that will be transmitted at base_tsc + wslot_width_tsc
  wheel.insert(dummy_ent, args.base_tsc, args.base_tsc);
  ASSERT_EQ(wheel.ready_queue.size(), 0);

  printf("reap at wslot_width_tsc - 1\n");
  wheel.reap(args.base_tsc + wheel.wslot_width_tsc - 1);
  ASSERT_EQ(wheel.ready_queue.size(), 0);
  ASSERT_EQ(wheel.cur_wslot, 0);

  printf("reap at wslot_width_tsc\n");
  wheel.reap(args.base_tsc + wheel.wslot_width_tsc);
  ASSERT_EQ(wheel.ready_queue.size(), 1);
  wheel.ready_queue.pop();
}

TEST(TimingWheelTest, RateTest) {
  HugeAlloc alloc(MB(2), 0, reg_mr_func, dereg_mr_func);
  timing_wheel_args_t args;
  args.mtu = kTestMTU;
  args.freq_ghz = measure_rdtsc_freq();
  args.base_tsc = rdtsc();
  args.wslot_width = kTestWslotWidth;
  args.huge_alloc = &alloc;

  TimingWheel wheel(args);
  const auto dummy_ent = wheel_ent_t(nullptr, 1);
  const size_t num_pkts = 10000;
  size_t num_pkts_sent = 0;
  size_t last_tsc = rdtsc();

  std::uniform_real_distribution<double> unif(kTimelyMinRate, kTimelyMaxRate);
  std::default_random_engine re;

  for (size_t iters = 0; iters < 5; iters++) {
    double target_rate = unif(re);
    test_printf("Target rate = %.2f Gbps\n", Timely::rate_to_gbps(target_rate));

    double ns_per_pkt = 1000000000 * (kTestMTU / target_rate);
    size_t cycles_per_pkt = round_up(args.freq_ghz * ns_per_pkt);

    size_t start_tsc = rdtsc();
    for (size_t i = 0; i < kSessionCredits; i++) {
      wheel.insert(dummy_ent, start_tsc, start_tsc + (i * cycles_per_pkt));
    }

    while (wheel.ready_queue.size() != kSessionCredits) wheel.reap(rdtsc());
    double seconds = to_sec(rdtsc() - start_tsc, args.freq_ghz);
    double achieved_rate = kSessionCredits * kTestMTU / seconds;

    test_printf("Achieved rate = %.2f Gbps\n",
                Timely::rate_to_gbps(achieved_rate));
    while (!wheel.ready_queue.empty()) wheel.ready_queue.pop();
  }
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

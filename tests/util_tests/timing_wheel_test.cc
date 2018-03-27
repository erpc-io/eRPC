#include <gtest/gtest.h>
#include <time.h>
#include <algorithm>
#include <vector>

#include "util/huge_alloc.h"
#include "util/test_printf.h"

#define private public
#include "cc/timing_wheel.h"

using namespace erpc;

static constexpr size_t kTestMTU = 1024;
static constexpr size_t kTestPktSize = 1024;
static constexpr size_t kTestNumPkts = 8000;
static constexpr double kTestWslotWidth = .2;  // us per wheel slot

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
  args.wslot_width = kTestWslotWidth;
  args.huge_alloc = &alloc;

  TimingWheel wheel(args);
  const auto dummy_ent = wheel_ent_t(nullptr, 1);

  // Empty wheel
  wheel.reap(rdtsc());
  ASSERT_EQ(wheel.ready_queue.size(), 0);

  // One entry. Check that it's eventually sent.
  wheel.insert(dummy_ent, rdtsc(), rdtsc() + wheel.wslot_width_tsc);

  while (true) {
    wheel.reap(rdtsc());
    if (wheel.ready_queue.size() > 0) break;
  }
}

TEST(TimingWheelTest, RateTest) {
  const std::vector<double> target_gbps = {1.0, 5.0, 10.0, 20.0, 40.0, 80.0};
  const double freq_ghz = measure_rdtsc_freq();

  for (size_t iters = 0; iters < target_gbps.size(); iters++) {
    const double target_rate = Timely::gbps_to_rate(target_gbps[iters]);
    test_printf("Target rate = %.2f Gbps\n", target_gbps[iters]);
    const double ns_per_pkt = 1000000000 * (kTestPktSize / target_rate);
    const size_t cycles_per_pkt = round_up(freq_ghz * ns_per_pkt);

    TscTimer rate_timer;

    // Create the a new wheel so we automatically clean up extra packets from
    // each iteration
    HugeAlloc alloc(MB(2), 0, reg_mr_func, dereg_mr_func);
    timing_wheel_args_t args;
    args.mtu = kTestMTU;
    args.freq_ghz = freq_ghz;
    args.wslot_width = kTestWslotWidth;
    args.huge_alloc = &alloc;

    TimingWheel wheel(args);
    const auto dummy_ent = wheel_ent_t(nullptr, 1);

    // Update the wheel and start measurement
    wheel.catchup();

    rate_timer.start();

    // Send one window
    size_t abs_tx_tsc = rdtsc();  // TX tsc for this session
    for (size_t i = 0; i < kSessionCredits; i++) {
      wheel.insert(dummy_ent, rdtsc(), abs_tx_tsc);
      abs_tx_tsc += cycles_per_pkt;
    }

    size_t num_pkts_sent = 0;
    while (num_pkts_sent < kTestNumPkts) {
      size_t cur_tsc = rdtsc();
      wheel.reap(cur_tsc);

      size_t num_ready = wheel.ready_queue.size();
      assert(num_ready <= kSessionCredits);

      if (num_ready > 0) {
        num_pkts_sent += num_ready;

        // Send more packets
        for (size_t i = 0; i < num_ready; i++) {
          wheel.insert(dummy_ent, rdtsc(), abs_tx_tsc);
          abs_tx_tsc += cycles_per_pkt;
          wheel.ready_queue.pop();
        }
      }
    }

    rate_timer.stop();
    double seconds = rate_timer.avg_sec(freq_ghz);
    double achieved_rate = num_pkts_sent * kTestPktSize / seconds;
    test_printf("Achieved rate = %.2f Gbps\n",
                Timely::rate_to_gbps(achieved_rate));
  }
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#include <gtest/gtest.h>
#include <time.h>
#include <algorithm>
#include <vector>

#include "util/huge_alloc.h"
#include "util/test_printf.h"

#define private public
#include "cc/timing_wheel.h"

using namespace erpc;

static constexpr size_t kTestPktSize = 1024;
static constexpr size_t kTestNumPkts = 8000;

// Dummy registration and deregistration functions
Transport::mem_reg_info reg_mr_wrapper(void *, size_t) {
  return Transport::mem_reg_info(0, 0);
}

void dereg_mr_wrapper(Transport::mem_reg_info) {}

using namespace std::placeholders;
typename Transport::reg_mr_func_t reg_mr_func =
    std::bind(reg_mr_wrapper, _1, _2);
typename Transport::dereg_mr_func_t dereg_mr_func =
    std::bind(dereg_mr_wrapper, _1);

class TimingWheelTest : public ::testing::Test {
 public:
  TimingWheelTest() {
    alloc_ = new HugeAlloc(MB(2), 0, reg_mr_func, dereg_mr_func);

    timing_wheel_args_t args;
    args.freq_ghz_ = measure_rdtsc_freq();
    args.huge_alloc_ = alloc_;
    wheel_ = new TimingWheel(args);

    freq_ghz_ = measure_rdtsc_freq();
  }

  ~TimingWheelTest() { delete alloc_; }

  HugeAlloc *alloc_;
  TimingWheel *wheel_;
  double freq_ghz_;
};

TEST_F(TimingWheelTest, Basic) {
  // Empty wheel
  wheel_->reap(rdtsc());
  ASSERT_EQ(wheel_->ready_queue_.size(), 0);

  // One entry. Check that it's eventually sent.
  size_t ref_tsc = rdtsc();
  size_t abs_tx_tsc = ref_tsc + wheel_->wslot_width_tsc_;
  wheel_->insert(TimingWheel::get_dummy_ent(), ref_tsc, abs_tx_tsc);

  wheel_->reap(abs_tx_tsc + wheel_->wslot_width_tsc_);
  ASSERT_EQ(wheel_->ready_queue_.size(), 1);
}

// This is not a fixture test because we use a different wheel for each rate
TEST(TimingWheelRateTest, RateTest) {
  const std::vector<double> target_gbps = {1.0, 5.0, 10.0, 20.0, 40.0, 80.0};
  const double freq_ghz = measure_rdtsc_freq();

  for (size_t iters = 0; iters < target_gbps.size(); iters++) {
    const double target_rate = Timely::gbps_to_rate(target_gbps[iters]);
    test_printf("Target rate = %.2f Gbps\n", target_gbps[iters]);
    const double ns_per_pkt = 1000000000 * (kTestPktSize / target_rate);
    const size_t cycles_per_pkt = erpc::ceil(freq_ghz * ns_per_pkt);

    TscTimer rate_timer;

    // Create the a new wheel so we automatically clean up extra packets from
    // each iteration
    HugeAlloc alloc(MB(2), 0, reg_mr_func, dereg_mr_func);
    timing_wheel_args_t args;
    args.freq_ghz_ = freq_ghz;
    args.huge_alloc_ = &alloc;

    TimingWheel wheel(args);

    // Update the wheel and start measurement
    wheel.catchup();
    rate_timer.start();

    size_t abs_tx_tsc = rdtsc();  // TX tsc for this session

    // Send one window
    size_t ref_tsc = rdtsc();
    for (size_t i = 0; i < kSessionCredits; i++) {
      abs_tx_tsc = std::max(ref_tsc, abs_tx_tsc + cycles_per_pkt);
      wheel.insert(TimingWheel::get_dummy_ent(), ref_tsc, abs_tx_tsc);
    }

    size_t num_pkts_sent = 0;
    while (num_pkts_sent < kTestNumPkts) {
      size_t cur_tsc = rdtsc();
      wheel.reap(cur_tsc);

      size_t num_ready = wheel.ready_queue_.size();
      assert(num_ready <= kSessionCredits);

      if (num_ready > 0) {
        num_pkts_sent += num_ready;

        for (size_t i = 0; i < num_ready; i++) wheel.ready_queue_.pop();

        // Send more packets
        ref_tsc = rdtsc();
        for (size_t i = 0; i < num_ready; i++) {
          abs_tx_tsc = std::max(ref_tsc, abs_tx_tsc + cycles_per_pkt);
          wheel.insert(TimingWheel::get_dummy_ent(), ref_tsc, abs_tx_tsc);
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

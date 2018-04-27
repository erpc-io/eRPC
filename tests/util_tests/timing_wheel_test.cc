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

class TimingWheelTest : public ::testing::Test {
 public:
  TimingWheelTest() {
    alloc = new HugeAlloc(MB(2), 0, reg_mr_func, dereg_mr_func);

    timing_wheel_args_t args;
    args.mtu = kTestMTU;
    args.freq_ghz = measure_rdtsc_freq();
    args.huge_alloc = alloc;
    wheel = new TimingWheel(args);

    freq_ghz = measure_rdtsc_freq();
  }

  ~TimingWheelTest() { delete alloc; }

  HugeAlloc *alloc;
  TimingWheel *wheel;
  double freq_ghz;
};

TEST_F(TimingWheelTest, Basic) {
  // Empty wheel
  wheel->reap(rdtsc());
  ASSERT_EQ(wheel->ready_queue.size(), 0);

  // One entry. Check that it's eventually sent.
  size_t ref_tsc = rdtsc();
  size_t abs_tx_tsc = ref_tsc + wheel->wslot_width_tsc;
  wheel->insert(TimingWheel::get_dummy_ent(), ref_tsc, abs_tx_tsc);

  wheel->reap(abs_tx_tsc + wheel->wslot_width_tsc);
  ASSERT_EQ(wheel->ready_queue.size(), 1);
}

TEST_F(TimingWheelTest, DeleteOne) {
  // Insert one entry and delete it
  wheel_ent_t ent = TimingWheel::get_dummy_ent();
  size_t ref_tsc = rdtsc();
  size_t abs_tx_tsc = ref_tsc + wheel->wslot_width_tsc;
  size_t wslot_idx = wheel->insert(ent, ref_tsc, abs_tx_tsc);
  wheel->delete_from_wslot(wslot_idx, reinterpret_cast<SSlot *>(ent.sslot));

  wheel->reap(abs_tx_tsc + wheel->wslot_width_tsc);
  ASSERT_EQ(wheel->ready_queue.size(), 0);
}

TEST_F(TimingWheelTest, DeleteMany) {
  // We'll insert around kNumEntriesPerWslot entries per wheel slot used
  static constexpr size_t kNumEntries = 1000;
  static constexpr size_t kNumEntriesPerWslot = kWheelBucketCap * 5;
  static_assert(kNumEntries < kWheelNumWslots, "");

  wheel_ent_t ent = TimingWheel::get_dummy_ent();
  std::array<uint16_t, kNumEntries> wslot_idx_arr;

  size_t ref_tsc = rdtsc();
  size_t delta = 0;
  for (size_t i = 0; i < kNumEntries; i++) {
    delta += (wheel->wslot_width_tsc / kNumEntriesPerWslot);
    wslot_idx_arr[i] = wheel->insert(ent, ref_tsc, ref_tsc + delta);
  }

  std::random_shuffle(wslot_idx_arr.begin(), wslot_idx_arr.end());
  for (size_t i = 0; i < kNumEntries; i++) {
    wheel->delete_from_wslot(wslot_idx_arr[i],
                             reinterpret_cast<SSlot *>(ent.sslot));
  }

  // (ref_tsc + delta) is the maximum requested absolute TX TSC
  wheel->reap((ref_tsc + delta) + wheel->wslot_width_tsc);
  ASSERT_EQ(wheel->ready_queue.size(), 0);
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
    args.mtu = kTestMTU;
    args.freq_ghz = freq_ghz;
    args.huge_alloc = &alloc;

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

      size_t num_ready = wheel.ready_queue.size();
      assert(num_ready <= kSessionCredits);

      if (num_ready > 0) {
        num_pkts_sent += num_ready;

        for (size_t i = 0; i < num_ready; i++) wheel.ready_queue.pop();

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

/**
 * @file timely.h
 * @brief TIMELY congestion control [SIGCOMM 15]
 * From: http://people.eecs.berkeley.edu/~radhika/timely-code-snippet.cc
 * Units: Microseconds or TSC for time, bytes/sec for throughput
 */

#ifndef ERPC_TIMELY_H
#define ERPC_TIMELY_H

#include "common.h"
#include "util/timer.h"

namespace erpc {
static constexpr double kTimelyEwmaAlpha = 0.02;

static constexpr double kTimelyMinRTT = 2;
static constexpr double kTimelyTLow = 50;
static constexpr double kTimelyTHigh = 1000;

static constexpr double kTimelyDecreaseFactor = 0.8;
static constexpr size_t kTimelyHaiThresh = 5;

/// Max = 5 GB/s (40 Gbps), min = 5 MB/s (arbitrary)
static constexpr double kTimelyMaxRate = 5.0 * 1000 * 1000 * 1000;
static constexpr double kTimelyMinRate = 5.0 * 1000 * 1000;
static constexpr double kTimelyAddRate = 5.0 * 1000 * 1000;

class Timely {
 private:
  size_t neg_gradient_count = 0;
  double prev_rtt = kTimelyMinRTT;
  double avg_rtt_diff = 0.0;
  size_t last_update_tsc = 0;
  double min_rtt_tsc = 0.0;
  double freq_ghz = 0.0;

 public:
  double rate = kTimelyMaxRate;

  Timely() {}
  Timely(double freq_ghz)
      : last_update_tsc(rdtsc()),
        min_rtt_tsc(kTimelyMinRTT * freq_ghz * 1000),
        freq_ghz(freq_ghz) {}

  /**
   * @brief Perform a rate update
   *
   * @param _rdtsc A recently sampled RDTSC. This can reduce calls to rdtsc().
   * @param sample_rtt_tsc The RTT sample in RDTSC cycles
   */
  void update_rate(size_t _rdtsc, size_t sample_rtt_tsc) {
    if (kDisableTimely) return;
    assert(_rdtsc >= 1000000000 && _rdtsc >= last_update_tsc);  // Sanity check

    // Sample RTT can be lower than min RTT during retransmissions
    if (unlikely(sample_rtt_tsc < min_rtt_tsc)) return;

    // Convert the sample RTT to usec, and don't use _sample_rtt_tsc from now
    double sample_rtt = to_usec(sample_rtt_tsc, freq_ghz);

    double rtt_diff = sample_rtt - prev_rtt;
    neg_gradient_count = (rtt_diff < 0) ? neg_gradient_count + 1 : 0;
    avg_rtt_diff =
        ((1 - kTimelyEwmaAlpha) * avg_rtt_diff) + (kTimelyEwmaAlpha * rtt_diff);

    double normalized_gradient = avg_rtt_diff / kTimelyMinRTT;

    double delta_factor = (_rdtsc - last_update_tsc) / min_rtt_tsc;  // fdiv
    delta_factor = std::min(delta_factor, 1.0);

    double new_rate;
    if (sample_rtt < kTimelyTLow) {
      // Additive increase
      new_rate = rate + (kTimelyAddRate * delta_factor);
    } else {
      if (unlikely(sample_rtt > kTimelyTHigh)) {
        // Multiplicative decrease based on current RTT sample, not average
        new_rate = rate * (1 - (delta_factor * kTimelyDecreaseFactor *
                                (1 - (kTimelyTHigh / sample_rtt))));
      } else {
        if (normalized_gradient <= 0) {
          // Additive increase, possibly hyper-active
          size_t N = neg_gradient_count >= kTimelyHaiThresh ? 5 : 1;
          new_rate = rate + (N * kTimelyAddRate * delta_factor);
        } else {
          // Multiplicative decrease based on moving average gradient
          new_rate = rate * (1.0 - (delta_factor * kTimelyDecreaseFactor *
                                    normalized_gradient));
        }
      }
    }

    prev_rtt = sample_rtt;
    last_update_tsc = _rdtsc;

    rate = std::max(new_rate, rate * 0.5);
    rate = std::min(rate, kTimelyMaxRate);
    rate = std::max(rate, kTimelyMinRate);
  }

  double get_avg_rtt_diff() const { return avg_rtt_diff; }
  double get_rate_gbps() const { return rate_to_gbps(rate); }

  /// Convert a default bytes/second rate to Gbit/s
  static double rate_to_gbps(double r) {
    return (r / (1000 * 1000 * 1000)) * 8;
  }

  /// Convert a Gbit/s rate to the default bytes/second
  static double gbps_to_rate(double r) {
    return (r / 8) * (1000 * 1000 * 1000);
  }
};
}  // End erpc

#endif  // ERPC_TIMELY_H

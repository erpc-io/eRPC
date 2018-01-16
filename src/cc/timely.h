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
/// Max = 5 GB/s (40 Gbps), min = 5 MB/s (arbitrary)
static constexpr double kTimelyMaxRate = 5.0 * 1000 * 1000 * 1000;
static constexpr double kTimelyMinRate = 5.0 * 1000 * 1000;
static constexpr double kTimelyAddRate = 5.0 * 1000 * 1000;

class Timely {
 private:
  static constexpr bool kPatched = true;  // Pacth from ECN-vs-delay
  static constexpr double kEwmaAlpha = 0.02;

  static constexpr double kMinRTT = 2;
  static constexpr double kTLow = 50;
  static constexpr double kTHigh = 1000;

  static constexpr double kBeta = 0.8;
  static constexpr size_t kHaiThresh = 5;

  size_t neg_gradient_count = 0;
  double prev_rtt = kMinRTT;
  double avg_rtt_diff = 0.0;
  size_t last_update_tsc = 0;
  double min_rtt_tsc = 0.0;
  double freq_ghz = 0.0;

 public:
  double rate = kTimelyMaxRate;

  Timely() {}
  Timely(double freq_ghz)
      : last_update_tsc(rdtsc()),
        min_rtt_tsc(kMinRTT * freq_ghz * 1000),
        freq_ghz(freq_ghz) {}

  // The w(g) function from ECN-vs-delay
  static double w_func(double g) {
    assert(kPatched);
    if (g <= -0.25) return 0;
    if (g >= 0.25) return 1;
    return (2 * g + 0.5);
  }

  /**
   * @brief Perform a rate update
   *
   * @param _rdtsc A recently sampled RDTSC. This can reduce calls to rdtsc()
   * when the caller can reuse a sampled RDTSC.
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
    avg_rtt_diff = ((1 - kEwmaAlpha) * avg_rtt_diff) + (kEwmaAlpha * rtt_diff);

    double _delta_factor = (_rdtsc - last_update_tsc) / min_rtt_tsc;  // fdiv
    _delta_factor = std::min(_delta_factor, 1.0);

    double ai_factor = kTimelyMaxRate * _delta_factor;

    double new_rate;
    if (sample_rtt < kTLow) {
      // Additive increase
      new_rate = rate + ai_factor;
    } else {
      double md_factor = _delta_factor * kBeta;   // Scaled factor for decrease
      double norm_grad = avg_rtt_diff / kMinRTT;  // Normalized gradient

      if (likely(sample_rtt <= kTHigh)) {
        if (kPatched) {
          double wght = w_func(norm_grad);
          double err = (sample_rtt - kMinRTT) / kMinRTT;
          rate = rate * (1 - md_factor * wght * err) + ai_factor * (1 - wght);
        } else {
          // Original logic
          if (norm_grad <= 0) {
            size_t N = neg_gradient_count >= kHaiThresh ? 5 : 1;
            new_rate = rate + N * ai_factor;
          } else {
            new_rate = rate * (1.0 - md_factor * norm_grad);
          }
        }
      } else {
        // Multiplicative decrease based on current RTT sample, not average
        new_rate = rate * (1 - md_factor * (1 - kTHigh / sample_rtt));
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

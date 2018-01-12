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

static constexpr double kTimelyMinRTT = 2.5;
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
  double prev_rtt = 0.0;
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

  void update_rate(size_t sample_rtt_tsc);
  double get_avg_rtt_diff() const { return avg_rtt_diff; }
  double get_rate_gbps() const { return rate_to_gbps(rate); }

  /// Convert a bytes/second rate to Gbps
  static double rate_to_gbps(double r) {
    return (r / (1000 * 1000 * 1000)) * 8;
  }
};
}  // End erpc

#endif  // ERPC_TIMELY_H

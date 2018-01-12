#include "cc/timely.h"

namespace erpc {
void Timely::update_rate(size_t _sample_rtt_tsc) {
  assert(_sample_rtt_tsc >= min_rtt_tsc);

  // Convert the sample RTT to usec, and don't use _sample_rtt_tsc from now
  double sample_rtt = to_usec(_sample_rtt_tsc, freq_ghz);
  if (unlikely(prev_rtt == 0.0)) prev_rtt = sample_rtt;

  double rtt_diff = sample_rtt - prev_rtt;
  neg_gradient_count = (rtt_diff < 0) ? neg_gradient_count + 1 : 0;
  avg_rtt_diff =
      ((1 - kTimelyEwmaAlpha) * avg_rtt_diff) + (kTimelyEwmaAlpha * rtt_diff);

  double normalized_gradient = avg_rtt_diff / kTimelyMinRTT;

  size_t cur_tsc = rdtsc();
  assert(cur_tsc > last_update_tsc);
  double delta_factor = (cur_tsc - last_update_tsc) / min_rtt_tsc;  // fdiv
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
  last_update_tsc = cur_tsc;

  rate = std::max(new_rate, rate * 0.5);
  rate = std::min(rate, kTimelyMaxRate);
  rate = std::max(rate, kTimelyMinRate);
}
}  // End erpc

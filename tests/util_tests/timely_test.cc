#include "timely.h"
using namespace erpc;

int main() {
  Timely timely(measure_rdtsc_freq());
  std::vector<double> sample_seq = {12,   13,   20, 20, 40, 2000, 2000,
                                    2000, 2000, 12, 20, 60, 20,   30,
                                    55,   60,   20, 20, 20, 20,   20};

  for (double rtt : sample_seq) {
    timely.update_rate(rtt);
    printf("RTT = %.2f, tput = %.2f Gbps, avg_rtt_diff = %.2f us\n", rtt,
           Timely::rate_to_gbps(timely.rate), timely.get_avg_rtt_diff());
  }
}

#include "cc/timely.h"
using namespace erpc;

int main() {
  double freq_ghz = measure_rdtsc_freq();
  Timely timely(freq_ghz);
  std::vector<double> sample_seq = {12,   13,   20, 20, 40, 2000, 2000,
                                    2000, 2000, 12, 20, 60, 20,   30,
                                    55,   60,   20, 20, 20, 20,   20};

  for (double rtt : sample_seq) {
    timely.update_rate(rtt);
    nano_sleep(1000, freq_ghz);  // Update every one microsecond
    printf("RTT = %.2f, tput = %.2f Gbps, avg_rtt_diff = %.2f us\n", rtt,
           timely.get_rate_gbps(), timely.get_avg_rtt_diff());
  }
}

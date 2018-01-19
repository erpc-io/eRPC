#include "cc/timely.h"
using namespace erpc;

int main() {
  double freq_ghz = measure_rdtsc_freq();
  Timely timely(freq_ghz);

  std::vector<double> sample_us;
  for (size_t i = 0; i < 2000; i++) {
    // double rtt_sample = 500 + (0.2 * i);
    double rtt_sample = 500 + rand() % 1;
    sample_us.push_back(rtt_sample);
  }

  for (double rtt_us : sample_us) {
    timely.update_rate(rdtsc(), us_to_cycles(rtt_us, freq_ghz));
    nano_sleep(1000, freq_ghz);  // Update every one microsecond
    printf("RTT = %.2f us, tput = %.2f Gbps, avg_rtt_diff = %.2f us\n", rtt_us,
           timely.get_rate_gbps(), timely.get_avg_rtt_diff());
  }
}

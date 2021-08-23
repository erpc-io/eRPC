/**
 * @file hdr_histogram_wrapper.h
 * A histogram good for microsecond-scale precision latency measurement
 */

#include <stdlib.h>
#include "HdrHistogram_c/src/hdr_histogram.h"

class LatencyUsHdrHistogram {
 public:
  static constexpr int64_t kMinLatencyMicros = 1;
  static constexpr int64_t kMaxLatencyMicros = 1000 * 1000 * 100;  // 100 sec
  static constexpr int64_t kLatencyPrecision = 2;  // Two significant digits
  LatencyUsHdrHistogram() {
    int ret = hdr_init(kMinLatencyMicros, kMaxLatencyMicros, kLatencyPrecision,
                       &hist);
    if (ret != 0) {
      fprintf(stderr, "Failed to init HDR histogram");
      exit(-1);
    }
  }

  ~LatencyUsHdrHistogram() { hdr_close(hist); }

  /// Add a value into the histogram
  inline void insert(double v) { hdr_record_value(hist, v); }

  /// Return the value at percentile 0.0 < p < 100.0
  double percentile(double p) {
    return hdr_value_at_percentile(hist, p);
  }

  /// Return the max value in the histogram
  double max() { return hdr_max(hist); }

  /// Clear the underlying histogram
  void reset() { hdr_reset(hist); }

  hdr_histogram* get_raw_hist() { return hist; }

 private:
  hdr_histogram* hist = nullptr;
};

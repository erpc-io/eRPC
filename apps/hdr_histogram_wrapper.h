/**
 * @file hdr_histogram_wrapper.h
 * @brief A wrapper for hdr_histogram that supports floating point values with
 * magnified precision. A floating point record x is inserted as x * AMP.
 */

#include <hdr/hdr_histogram.h>
#include "apps_common.h"

template <size_t AMP>
class HdrHistogramAmp {
 public:
  HdrHistogramAmp(int64_t min, int64_t max, uint32_t precision) {
    int ret = hdr_init(min * AMP, max * AMP, precision, &hist);
    erpc::rt_assert(ret == 0);
  }

  ~HdrHistogramAmp() { hdr_close(hist); }

  inline void record_value(double v) { hdr_record_value(hist, v * AMP); }

  void percentile(double p) { return hdr_value_at_percentile(hist, p) * AMP; }

  void reset() { hdr_reset(hist); }

  hdr_histogram* get_raw_hist() { return hist; }

 private:
  hdr_histogram* hist = nullptr;
};

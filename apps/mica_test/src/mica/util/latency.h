#pragma once
#ifndef MICA_UTIL_LATENCY_H_
#define MICA_UTIL_LATENCY_H_

#include <cstdio>
#include <algorithm>
#include "mica/common.h"
#include "mica/util/memcpy.h"

namespace mica {
namespace util {
class Latency {
 public:
  Latency() { reset(); }

  void reset() { ::mica::util::memset(this, 0, sizeof(Latency)); }

  void update(uint64_t us) {
    if (us < 128)
      bin0_[us]++;
    else if (us < 384)
      bin1_[(us - 128) / 2]++;
    else if (us < 896)
      bin2_[(us - 384) / 4]++;
    else if (us < 1920)
      bin3_[(us - 896) / 8]++;
    else if (us < 3968)
      bin4_[(us - 1920) / 16]++;
    else
      bin5_++;
  }

  Latency& operator+=(const Latency& o) {
    uint64_t i;
    for (i = 0; i < 128; i++) bin0_[i] += o.bin0_[i];
    for (i = 0; i < 128; i++) bin1_[i] += o.bin1_[i];
    for (i = 0; i < 128; i++) bin2_[i] += o.bin2_[i];
    for (i = 0; i < 128; i++) bin3_[i] += o.bin3_[i];
    for (i = 0; i < 128; i++) bin4_[i] += o.bin4_[i];
    bin5_ += o.bin5_;
    return *this;
  }

  uint64_t count() const {
    uint64_t count = 0;
    uint64_t i;
    for (i = 0; i < 128; i++) count += bin0_[i];
    for (i = 0; i < 128; i++) count += bin1_[i];
    for (i = 0; i < 128; i++) count += bin2_[i];
    for (i = 0; i < 128; i++) count += bin3_[i];
    for (i = 0; i < 128; i++) count += bin4_[i];
    count += bin5_;
    return count;
  }

  uint64_t sum() const {
    uint64_t sum = 0;
    uint64_t i;
    for (i = 0; i < 128; i++) sum += bin0_[i] * (0 + i * 1);
    for (i = 0; i < 128; i++) sum += bin1_[i] * (128 + i * 2);
    for (i = 0; i < 128; i++) sum += bin2_[i] * (384 + i * 4);
    for (i = 0; i < 128; i++) sum += bin3_[i] * (896 + i * 8);
    for (i = 0; i < 128; i++) sum += bin4_[i] * (1920 + i * 16);
    sum += bin5_ * 3968;
    return sum;
  }

  uint64_t avg() const { return sum() / std::max(uint64_t(1), count()); }

  uint64_t min() const {
    uint64_t i;
    for (i = 0; i < 128; i++)
      if (bin0_[i] != 0) return 0 + i * 1;
    for (i = 0; i < 128; i++)
      if (bin1_[i] != 0) return 128 + i * 2;
    for (i = 0; i < 128; i++)
      if (bin2_[i] != 0) return 384 + i * 4;
    for (i = 0; i < 128; i++)
      if (bin3_[i] != 0) return 896 + i * 8;
    for (i = 0; i < 128; i++)
      if (bin4_[i] != 0) return 1920 + i * 16;
    // if (bin5_ != 0) return 3968;
    return 3968;
  }

  uint64_t max() const {
    int64_t i;
    if (bin5_ != 0) return 3968;
    for (i = 127; i >= 0; i--)
      if (bin4_[i] != 0) return 1920 + static_cast<uint64_t>(i) * 16;
    for (i = 127; i >= 0; i--)
      if (bin3_[i] != 0) return 896 + static_cast<uint64_t>(i) * 8;
    for (i = 127; i >= 0; i--)
      if (bin2_[i] != 0) return 384 + static_cast<uint64_t>(i) * 4;
    for (i = 127; i >= 0; i--)
      if (bin1_[i] != 0) return 128 + static_cast<uint64_t>(i) * 2;
    for (i = 127; i >= 0; i--)
      if (bin0_[i] != 0) return 0 + static_cast<uint64_t>(i) * 1;
    return 0;
  }

  // Return the (p * 100) percentile latency
  uint64_t perc(double p) const {
    assert(p >= 0.0 && p <= 1.00);

    uint64_t i;
    int64_t thres = static_cast<int64_t>(p * static_cast<double>(count()));
    for (i = 0; i < 128; i++)
      if ((thres -= static_cast<int64_t>(bin0_[i])) < 0) return 0 + i * 1;
    for (i = 0; i < 128; i++)
      if ((thres -= static_cast<int64_t>(bin1_[i])) < 0) return 128 + i * 2;
    for (i = 0; i < 128; i++)
      if ((thres -= static_cast<int64_t>(bin2_[i])) < 0) return 384 + i * 4;
    for (i = 0; i < 128; i++)
      if ((thres -= static_cast<int64_t>(bin3_[i])) < 0) return 896 + i * 8;
    for (i = 0; i < 128; i++)
      if ((thres -= static_cast<int64_t>(bin4_[i])) < 0) return 1920 + i * 16;
    return 3968;
  }

  void print(FILE* fp) const {
    uint64_t i;
    for (i = 0; i < 128; i++)
      if (bin0_[i] != 0)
        fprintf(fp, "%4" PRIu64 " %6" PRIu64 "\n", 0 + i * 1, bin0_[i]);
    for (i = 0; i < 128; i++)
      if (bin1_[i] != 0)
        fprintf(fp, "%4" PRIu64 " %6" PRIu64 "\n", 128 + i * 2, bin1_[i]);
    for (i = 0; i < 128; i++)
      if (bin2_[i] != 0)
        fprintf(fp, "%4" PRIu64 " %6" PRIu64 "\n", 384 + i * 4, bin2_[i]);
    for (i = 0; i < 128; i++)
      if (bin3_[i] != 0)
        fprintf(fp, "%4" PRIu64 " %6" PRIu64 "\n", 896 + i * 8, bin3_[i]);
    for (i = 0; i < 128; i++)
      if (bin4_[i] != 0)
        fprintf(fp, "%4" PRIu64 " %6" PRIu64 "\n", 1920 + i * 16, bin4_[i]);
    if (bin5_ != 0) fprintf(fp, "%4d %6" PRIu64 "\n", 3968, bin5_);
  }

 private:
  // [0, 128) us
  uint64_t bin0_[128];
  // [128, 384) us
  uint64_t bin1_[128];
  // [384, 896) us
  uint64_t bin2_[128];
  // [896, 1920) us
  uint64_t bin3_[128];
  // [1920, 3968) us
  uint64_t bin4_[128];
  // [3968, inf) us
  uint64_t bin5_;
};
}
}

#endif

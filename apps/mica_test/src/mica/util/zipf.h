#pragma once
#ifndef MICA_UTIL_ZIPF_H_
#define MICA_UTIL_ZIPF_H_

#include "mica/common.h"
#include "mica/util/rand.h"

namespace mica {
namespace util {
class ZipfGen {
 public:
  ZipfGen(uint64_t n, double theta, uint64_t rand_seed);
  ZipfGen(const ZipfGen& src);
  ZipfGen(const ZipfGen& src, uint64_t rand_seed);
  ZipfGen& operator=(const ZipfGen& src);

  void change_n(uint64_t n);
  uint64_t next();

  static void test(double theta);

 private:
  static double pow_approx(double a, double b);

  static double zeta(uint64_t last_n, double last_sum, uint64_t n,
                     double theta);

  uint64_t n_;  // number of items (input)
  double
      theta_;  // skewness (input) in (0, 1); or, 0 = uniform, 1 = always zero
  double alpha_;     // only depends on theta
  double thres_;     // only depends on theta
  uint64_t last_n_;  // last n used to calculate the following
  double dbl_n_;
  double zetan_;
  double eta_;
  // unsigned short rand_state[3];    // prng state
  uint64_t seq_;  // for sequential number generation
  Rand rand_;
} __attribute__((aligned(128)));  // To prevent false sharing caused by
                                  // adjacent cacheline prefetching.
}
}

#endif
#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>
#include "common.h"

namespace erpc {

template <typename T>
static constexpr inline bool is_power_of_two(T x) {
  return x && ((x & T(x - 1)) == 0);
}

template <uint64_t power_of_two_number, typename T>
static constexpr inline T round_up(T x) {
  static_assert(is_power_of_two(power_of_two_number),
                "PowerOfTwoNumber must be a power of 2");
  return ((x) + T(power_of_two_number - 1)) & (~T(power_of_two_number - 1));
}

/// Return the index of the least significant bit of x. The index of the 2^0
/// bit is 1. (x = 0 returns 0, x = 1 returns 1.)
static inline size_t lsb_index(int x) {
  assert(x != 0);
  return static_cast<size_t>(__builtin_ffs(x));
}

/// Return the index of the most significant bit of x. The index of the 2^0
/// bit is 1. (x = 0 returns 0, x = 1 returns 1.)
static inline size_t msb_index(int x) {
  assert(x < INT32_MAX / 2);
  int index;
  asm("bsrl %1, %0" : "=r"(index) : "r"(x << 1));
  return static_cast<size_t>(index);
}

/// C++11 constexpr ceil
static constexpr size_t ceil(double num) {
  return (static_cast<double>(static_cast<size_t>(num)) == num)
             ? static_cast<size_t>(num)
             : static_cast<size_t>(num) + ((num > 0) ? 1 : 0);
}

/// Compute the standard deviation of a vector
static double stddev(std::vector<double> v) {
  if (unlikely(v.empty())) return 0;
  double sum = std::accumulate(v.begin(), v.end(), 0.0);
  double mean = sum / v.size();
  double sq_sum = std::inner_product(v.begin(), v.end(), v.begin(), 0.0);
  double var = sq_sum / v.size() - (mean * mean);
  if (unlikely(var < 0)) return 0.0;  // This can happen when var ~ 0

  return std::sqrt(var);
}

}  // namespace erpc

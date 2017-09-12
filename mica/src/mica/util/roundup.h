#pragma once
#ifndef MICA_UTIL_ROUNDUP_H_
#define MICA_UTIL_ROUNDUP_H_

#include "mica/common.h"

namespace mica {
namespace util {
template <typename T>
static constexpr bool is_power_of_two(T x) {
  return x && ((x & T(x - 1)) == 0);
}

template <uint64_t PowerOfTwoNumber, typename T>
static constexpr T roundup(T x) {
  static_assert(is_power_of_two(PowerOfTwoNumber),
                "PowerOfTwoNumber must be a power of 2");
  return ((x) + T(PowerOfTwoNumber - 1)) & (~T(PowerOfTwoNumber - 1));
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winline"

template <typename T>
static constexpr T next_power_of_two_recursive(T x, T s) {
  return ((T(1) << s) < x) ? next_power_of_two_recursive(x, s + 1)
                           : (T(1) << s);
}

template <typename T>
static constexpr T next_power_of_two(T x) {
  // T s(0);
  // while ((T(1) << s) < x) s++;
  // return T(1) << s;
  return next_power_of_two_recursive(x, T(0));
}

#pragma GCC diagnostic pop
}
}

#endif
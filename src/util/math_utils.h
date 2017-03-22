#ifndef ERPC_MATH_UTILS_H
#define ERPC_MATH_UTILS_H

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits>

namespace ERpc {

/// Optimized (x + 1) % N
template <size_t N>
static constexpr size_t mod_add_one(size_t x) {
  return (x + 1) == N ? 0 : x + 1;
}

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
  assert(x < std::numeric_limits<int>::max() / 2);
  int index;
  asm("bsrl %1, %0" : "=r"(index) : "r"(x << 1));
  return static_cast<size_t>(index);
}

}  /// End ERpc

#endif  // ERPC_MATH_UTILS_H

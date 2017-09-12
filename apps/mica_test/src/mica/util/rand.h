#pragma once
#ifndef MICA_UTIL_RAND_H_
#define MICA_UTIL_RAND_H_

#include <cassert>
#include "mica/common.h"

namespace mica {
namespace util {
class Rand {
 public:
  explicit Rand() : state_(0) {}
  explicit Rand(uint64_t seed) : state_(seed) { assert(seed < (1UL << 48)); }
  Rand(const Rand& o) : state_(o.state_) {}
  Rand& operator=(const Rand& o) {
    state_ = o.state_;
    return *this;
  }

  uint32_t next_u32() {
    // same as Java's
    state_ = (state_ * 0x5deece66dUL + 0xbUL) & ((1UL << 48) - 1);
    return (uint32_t)(state_ >> (48 - 32));
  }

  double next_f64() {
    // caution: this is maybe too non-random
    state_ = (state_ * 0x5deece66dUL + 0xbUL) & ((1UL << 48) - 1);
    return (double)state_ / (double)((1UL << 48) - 1);
  }

 private:
  uint64_t state_;
};
}
}

#endif
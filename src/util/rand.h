#pragma once

#include <random>

namespace erpc {

class SlowRand {
  std::random_device rand_dev_;  // Non-pseudorandom seed for twister
  std::mt19937_64 mt_;
  std::uniform_int_distribution<uint64_t> dist_;

 public:
  SlowRand() : mt_(rand_dev_()), dist_(0, UINT64_MAX) {}

  inline uint64_t next_u64() { return dist_(mt_); }
};

class FastRand {
 public:
  uint64_t seed_;

  /// Create a FastRand using a seed from SlowRand
  FastRand() {
    SlowRand slow_rand;
    seed_ = slow_rand.next_u64();
  }

  inline uint32_t next_u32() {
    seed_ = seed_ * 1103515245 + 12345;
    return static_cast<uint32_t>(seed_ >> 32);
  }
};

}  // namespace erpc

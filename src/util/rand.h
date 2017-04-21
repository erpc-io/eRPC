#ifndef ERPC_RAND_H
#define ERPC_RAND_H

#include <random>

namespace ERpc {

class SlowRand {
  std::random_device rand_dev; // Non-pseudorandom seed for twister
  std::mt19937_64 mt;
  std::uniform_int_distribution<uint64_t> dist;

 public:
  SlowRand() : mt(rand_dev()), dist(0, UINT64_MAX) {}

  inline uint64_t next_u64() { return dist(mt); }
};

class FastRand {
 public:
  uint64_t seed;

  /// Create a FastRand using a seed from SlowRand
  FastRand() {
    SlowRand slow_rand;
    seed = slow_rand.next_u64();
  }

  inline uint32_t next_u32() {
    seed = seed * 1103515245 + 12345;
    return static_cast<uint32_t>(seed >> 32);
  }
};

}  // End ERpc

#endif  // ERPC_RAND_H

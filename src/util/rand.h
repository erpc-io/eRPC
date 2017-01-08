#ifndef ERPC_RAND_H
#define ERPC_RAND_H

#include <limits.h>
#include <random>

namespace ERpc {

class SlowRand {
  std::random_device rand_dev; /* Non-pseudorandom seed for twister */
  std::mt19937_64 mt;
  std::uniform_int_distribution<uint64_t> dist;

 public:
  SlowRand();
  uint64_t next_u64();
};

class FastRand {
 public:
  uint64_t seed;

  FastRand(uint64_t seed);
  uint64_t next_u64();
};

}  // End ERpc

#endif  // ERPC_RAND_H

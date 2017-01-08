#include "rand.h"

namespace ERpc {

SlowRand::SlowRand() : mt(rand_dev()), dist(0, UINT64_MAX) {}

uint64_t SlowRand::next_u64() { return dist(mt); }

}  // End ERpc

/**
 * @file config.h
 * @brief Compile-time constants mostly derived from CMake
 */
#ifndef ERPC_CONFIG_H
#define ERPC_CONFIG_H

#include <assert.h>
#include <stdlib.h>

namespace erpc {

class IBTransport;
class RawTransport;

static constexpr bool kCC = true;  ///< Enable Timely and wheel-based pacing
static constexpr bool kDisableTimely = false;  ///< Disable Timely rate update

// static constexpr size_t kHeadroom = 0;
// typedef IBTransport CTransport;

static constexpr size_t kHeadroom = 40;
typedef RawTransport CTransport;

#if defined(TESTING)
static constexpr bool kTesting = true;
#else
static constexpr bool kTesting = false;
#endif

static constexpr bool kDatapathStats = false;
static inline void dpath_stat_inc(size_t &stat, size_t val) {
  if (kDatapathStats) stat += val;
}
}

#endif  // ERPC_CONFIG_H

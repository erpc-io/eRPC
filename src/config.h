/**
 * @file config.h
 * @brief Compile-time constants mostly derived from CMake
 */
#ifndef ERPC_CONFIG_H
#define ERPC_CONFIG_H

#include <assert.h>
#include <stdlib.h>

namespace erpc {

// Set the transport
#if defined(INFINIBAND)
static constexpr size_t kHeadroom = 0;
#elif defined(ROCE)
static constexpr size_t kHeadroom = 0;
#elif defined(RAW_ETHERNET)
static constexpr size_t kHeadroom = 40;
#else
static constexpr size_t kHeadroom = 40;
#endif

static constexpr bool kDatapathStats = false;

static inline void dpath_stat_inc(size_t &stat, size_t val) {
  if (kDatapathStats) stat += val;
}

#if defined(TESTING)
static constexpr bool kTesting = true;
#else
static constexpr bool kTesting = false;
#endif
}

#endif  // ERPC_CONFIG_H

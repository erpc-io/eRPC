/**
 * @file optlevel.h
 * @brief Defines for compile-time optimizations
 */
#ifndef OPTLEVEL_H
#define OPTLEVEL_H

#include <assert.h>
#include <stdlib.h>

namespace erpc {

static constexpr bool kDatapathStats = false;

static inline void dpath_stat_inc(size_t &stat, size_t val) {
  if (kDatapathStats) stat += val;
}

#ifdef TESTING
static constexpr bool kTesting = true;
#else
static constexpr bool kTesting = false;
#endif
}

#endif  // OPTLEVEL_H

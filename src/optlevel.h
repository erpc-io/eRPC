/**
 * @file optlevel.h
 * @brief Defines for compile-time optimizations
 */
#ifndef OPTLEVEL_H
#define OPTLEVEL_H

#include <assert.h>
#include <stdlib.h>

namespace erpc {

// Tweak these options for small RPC performance
#define small_rpc_optlevel (small_rpc_optlevel_none)
static constexpr bool kDatapathStats = false;   ///< Collect datapath stats
static constexpr bool kDatapathChecks = false;  ///< Extra datapath checks

///@{
/// Optimization level for small RPCs and foreground request handlers
/// 0 (none): No optimization
/// 1 (likely): Small RPCs and foreground request handlers are very likely
/// 2 (extreme): Large messages & background request handlers are not supported
#define small_rpc_optlevel_none (0)
#define small_rpc_optlevel_likely (1)
#define small_rpc_optlevel_extreme (2)
///@}

#define optlevel_large_rpc_supported \
  (small_rpc_optlevel != small_rpc_optlevel_extreme)

#if small_rpc_optlevel == small_rpc_optlevel_none
#define small_rpc_likely(x) (x)
#define small_rpc_unlikely(x) (x)
#elif small_rpc_optlevel == small_rpc_optlevel_likely
#define small_rpc_likely(x) likely(x)
#define small_rpc_unlikely(x) unlikely(x)
#elif small_rpc_optlevel == small_rpc_optlevel_extreme
static constexpr bool small_rpc_likely(bool x) {
  ((void)(x));
  assert(x);
  return true;
}
static constexpr bool small_rpc_unlikely(bool x) {
  ((void)(x));
  assert(!x);
  return false;
}
#else
static_assert(false, "");  // Invalid value of small_rpc_optlevel
#endif

/// Return true iff large Rpcs are supported
static constexpr bool large_rpc_supported() {
  return (small_rpc_optlevel != small_rpc_optlevel_extreme);
}

/// Collect datapath if enabled
static inline constexpr void dpath_stat_inc(size_t &stat, size_t val) {
  if (!kDatapathStats) return;
  stat += val;
}

/// Fault injection code that can be disabled for non-tests
#ifdef FAULT_INJECTION
static constexpr bool kFaultInjection = true;
#else
static constexpr bool kFaultInjection = false;
#endif
}

#endif  // OPTLEVEL_H

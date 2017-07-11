/**
 * @file optlevel.h
 * @brief Defines for compile-time optimizations
 */
#ifndef OPTLEVEL_H
#define OPTLEVEL_H

#include <assert.h>

namespace ERpc {

// Optimizations for small RPCs

/// No optimization for small messages and foreground request handlers
///@{
/// Optimization level for small RPCs and foreground request handlers
/// 0 (none): No optimization
/// 1 (likely): Small RPCs and foreground request handlers are very likely
/// 2 (extreme): Large messages & background request handlers are not supported
#define small_rpc_optlevel_none (0)
#define small_rpc_optlevel_likely (1)
#define small_rpc_optlevel_extreme (2)
#define small_rpc_optlevel (small_rpc_optlevel_likely)
///@}

#define optlevel_large_rpc_supported \
  (small_rpc_optlevel != small_rpc_optlevel_extreme)

static bool large_rpc_supported() {
   return (small_rpc_optlevel != small_rpc_optlevel_extreme);
}

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

/// Fault injection code that can be disabled for non-tests
#ifdef FAULT_INJECTION
static constexpr bool kFaultInjection = true;
#else
static constexpr bool kFaultInjection = false;
#endif
}

#endif  // OPTLEVEL_H

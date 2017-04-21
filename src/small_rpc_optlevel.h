#ifndef SMALL_RPC_OPTLEVEL_H
#define SMALL_RPC_OPTLEVEL_H

namespace ERpc {

/// No optimization for small messages and foreground request handlers
#define small_rpc_optlevel_none (0)

/// Small messages and foreground request handlers are very likely
#define small_rpc_optlevel_likely (1)

/// Large messages and background request handlers are not supported
#define small_rpc_optlevel_extreme (2)

/// This controls how much the code is optimized at compile time for the common
/// case of small messages and foreground request handlers. This helps
/// understand the overhead of supporting large messages and background threads.
#define small_rpc_optlevel (small_rpc_optlevel_likely)

#define optlevel_large_rpc_supported \
  (small_rpc_optlevel != small_rpc_optlevel_extreme)

#if small_rpc_optlevel == small_rpc_optlevel_none
#define small_rpc_likely(x) (x)
#define small_rpc_unlikely(x) (x)
#elif small_rpc_optlevel == small_rpc_optlevel_likely
#define small_rpc_likely(x) likely(x)
#define small_rpc_unlikely(x) unlikely(x)
#elif small_rpc_optlevel == small_rpc_optlevel_extreme
static constexpr bool small_rpc_likely(x) {
  assert(x);
  return true;
}
static constexpr bool small_rpc_unlikely(x) {
  assert(!x);
  return false;
}
#else
static_assert(false, "");
#endif
}

#endif  // SMALL_RPC_OPTLEVEL_H

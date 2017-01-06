#ifndef ERPC_COMMON_H
#define ERPC_COMMON_H

// Header file with convenience defines/functions that is included everywhere

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ERpc {

// Debug printing for ERPC classes without special needs.
#define ERPC_DPRINTF 1
#define erpc_dprintf(fmt, ...)           \
  do {                                   \
    if (ERPC_DPRINTF) {                  \
      fprintf(stderr, fmt, __VA_ARGS__); \
      fflush(stderr);                    \
    }                                    \
  } while (0)

// Debug-mode printing for classes with special needs
#define RPC_DPRINTF 1

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define forceinline inline __attribute__((always_inline))
#define _unused(x) ((void)(x)) /* Make production builds happy */

#define KB(x) ((size_t)(x) << 10)
#define KB_(x) (KB(x) - 1)
#define MB(x) ((size_t)(x) << 20)
#define MB_(x) (MB(x) - 1)

static const size_t kMaxNumaNodes = 16; /* Maximum number of NUMA nodes */
static const size_t kPageSize = 4096;   /* Page size in bytes */
static const size_t kHugepageSize = (2 * 1024 * 1024); /* Hugepage size */

static uint64_t RdTsc() {
  uint64_t rax;
  uint64_t rdx;
  asm volatile("rdtsc" : "=a"(rax), "=d"(rdx));
  return (rdx << 32) | rax;
}

template <typename T>
static constexpr bool IsPowerOfTwo(T x) {
  return x && ((x & T(x - 1)) == 0);
}

template <uint64_t PowerOfTwoNumber, typename T>
static constexpr T RoundUp(T x) {
  static_assert(IsPowerOfTwo(PowerOfTwoNumber),
                "PowerOfTwoNumber must be a power of 2");
  return ((x) + T(PowerOfTwoNumber - 1)) & (~T(PowerOfTwoNumber - 1));
}
}

#endif

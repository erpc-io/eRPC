#ifndef ERPC_COMMON_H
#define ERPC_COMMON_H

// Header file with convenience defines/functions that is included everywhere

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits>

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

#define erpc_dprintf_noargs(fmt) \
  do {                           \
    if (ERPC_DPRINTF) {          \
      fprintf(stderr, fmt);      \
      fflush(stderr);            \
    }                            \
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

// General typedefs and structs
struct udp_config_t {
  /*
   * The UDP port used by all Nexus-es in the cluster to listen on for
   * session management
   */
  uint16_t mgmt_udp_port;
  double drop_prob; /* Used to add packet loss to UDP traffic */

  udp_config_t(uint16_t mgmt_udp_port, double drop_prob)
      : mgmt_udp_port(mgmt_udp_port), drop_prob(drop_prob) {}
};

// General constants
static const size_t kMaxNumaNodes = 16; /* Maximum number of NUMA nodes */
static const size_t kPageSize = 4096;   /* Page size in bytes */
static const size_t kHugepageSize = (2 * 1024 * 1024); /* Hugepage size */
static const size_t kMaxPhyPorts = 4; /* Max fabric device ports */
static const size_t kMaxHostnameLen = 128;
static const size_t kMaxIssueMsgLen = /* Debug issue messages */
    (240 + kMaxHostnameLen * 2);      /* Three lines and two hostnames */

// Simple methods
static uint64_t rdtsc() {
  uint64_t rax;
  uint64_t rdx;
  asm volatile("rdtsc" : "=a"(rax), "=d"(rdx));
  return (rdx << 32) | rax;
}

/**
 * @brief Convert cycles measured by rdtsc with frequence \p freq_ghz to seconds
 */
static double to_sec(uint64_t cycles, double freq_ghz) {
  return (cycles / (freq_ghz * 1000000000));
}

/**
 * @brief Convert cycles measured by rdtsc with frequence \p freq_ghz to msec
 */
static double to_nsec(uint64_t cycles, double freq_ghz) {
  return (cycles / freq_ghz);
}

template <typename T>
static constexpr bool is_power_of_two(T x) {
  return x && ((x & T(x - 1)) == 0);
}

template <uint64_t power_of_two_number, typename T>
static constexpr T round_up(T x) {
  static_assert(is_power_of_two(power_of_two_number),
                "PowerOfTwoNumber must be a power of 2");
  return ((x) + T(power_of_two_number - 1)) & (~T(power_of_two_number - 1));
}

}  // End ERpc

#endif

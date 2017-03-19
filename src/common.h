#ifndef ERPC_COMMON_H
#define ERPC_COMMON_H

// Header file with convenience defines/functions that is included everywhere

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits>
#include <sstream>
#include <string>

namespace ERpc {

// Debug defines
static constexpr bool kVerbose = true;  ///< Debug printing for non-datapath
static constexpr bool kDatapathVerbose = true;  ///< Debug printing in datapath
static constexpr bool kDatapathStats = true;  ///< Collect stats on the datapath

// Perf defines
static constexpr bool kDatapathChecks = true;  ///< Return error on invalid args
#define small_msg_opt_level 1

/// Low-frequency debug message printing (e.g., session management messages)
#define erpc_dprintf(fmt, ...)           \
  do {                                   \
    if (kVerbose) {                      \
      fprintf(stderr, fmt, __VA_ARGS__); \
      fflush(stderr);                    \
    }                                    \
  } while (0)

#define erpc_dprintf_noargs(fmt) \
  do {                           \
    if (kVerbose) {              \
      fprintf(stderr, fmt);      \
      fflush(stderr);            \
    }                            \
  } while (0)

/// High-frequency debug message printing (e.g., fabric RX and TX)
#define dpath_dprintf(fmt, ...)          \
  do {                                   \
    if (kDatapathVerbose) {              \
      fprintf(stderr, fmt, __VA_ARGS__); \
      fflush(stderr);                    \
    }                                    \
  } while (0)

#define dpath_dprintf_noargs(fmt) \
  do {                            \
    if (kDatapathVerbose) {       \
      fprintf(stderr, fmt);       \
      fflush(stderr);             \
    }                             \
  } while (0)

#define _unused(x) ((void)(x)) /* Make production build happy */
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// Level of optimizations for small messages. This helps understand the overhead
// of supporting large messages

#if small_msg_opt_level == 0
/* No optimization for small message */
#define small_msg_likely(x) (x)
#define small_msg_unlikely(x) (x)
/* Small messages are very likely */
#elif small_msg_opt_level == 1
#define small_msg_likely(x) likely(x)
#define small_msg_unlikely(x) unlikely(x)
#else
/* Small messages are the only type of messages */
#define small_msg_likely(x) (true)
#define small_msg_unlikely(x) (false)
#endif

#define KB(x) ((size_t)(x) << 10)
#define KB_(x) (KB(x) - 1)
#define MB(x) ((size_t)(x) << 20)
#define MB_(x) (MB(x) - 1)

/// UDP config used throughout eRPC
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
static const size_t kMaxNumaNodes = 8; /* Maximum number of NUMA nodes */
static const size_t kPageSize = 4096;  /* Page size in bytes */
static const size_t kHugepageSize = (2 * 1024 * 1024); /* Hugepage size */
static const size_t kMaxPhyPorts = 4; /* Max fabric device ports */
static const size_t kMaxHostnameLen = 128;
static const size_t kMaxIssueMsgLen = /* Debug issue messages */
    (240 + kMaxHostnameLen * 2);      /* Three lines and two hostnames */

// Simple methods

/// Return the TSC
static inline uint64_t rdtsc() {
  uint64_t rax;
  uint64_t rdx;
  asm volatile("rdtsc" : "=a"(rax), "=d"(rdx));
  return (rdx << 32) | rax;
}

/// Convert cycles measured by rdtsc with frequence \p freq_ghz to seconds
static double to_sec(uint64_t cycles, double freq_ghz) {
  return (cycles / (freq_ghz * 1000000000));
}

/// Convert cycles measured by rdtsc with frequence \p freq_ghz to msec
static double to_msec(uint64_t cycles, double freq_ghz) {
  return (cycles / (freq_ghz * 1000000));
}

/// Convert cycles measured by rdtsc with frequence \p freq_ghz to usec
static double to_usec(uint64_t cycles, double freq_ghz) {
  return (cycles / (freq_ghz * 1000));
}

/// Convert cycles measured by rdtsc with frequence \p freq_ghz to nsec
static double to_nsec(uint64_t cycles, double freq_ghz) {
  return (cycles / freq_ghz);
}

/// Emulab hostnames are very long, so trim it to just the node name.
static std::string trim_hostname(std::string hostname) {
  if (hostname.find("emulab.net") != std::string::npos) {
    std::string trimmed_hostname = hostname.substr(0, hostname.find("."));
    return trimmed_hostname;
  } else {
    return hostname;
  }
}

/// Optimized (x + 1) % N
template <size_t N>
static constexpr size_t mod_add_one(size_t x) {
  return (x + 1) == N ? 0 : x + 1;
}

template <typename T>
static constexpr inline bool is_power_of_two(T x) {
  return x && ((x & T(x - 1)) == 0);
}

template <uint64_t power_of_two_number, typename T>
static constexpr inline T round_up(T x) {
  static_assert(is_power_of_two(power_of_two_number),
                "PowerOfTwoNumber must be a power of 2");
  return ((x) + T(power_of_two_number - 1)) & (~T(power_of_two_number - 1));
}

/// Return the index of the least significant bit of x. The index of the 2^0
/// bit is 1. (x = 0 returns 0, x = 1 returns 1.)
static inline size_t lsb_index(int x) {
  assert(x != 0);
  return static_cast<size_t>(__builtin_ffs(x));
}

/// Return the index of the most significant bit of x. The index of the 2^0
/// bit is 1. (x = 0 returns 0, x = 1 returns 1.)
static inline size_t msb_index(int x) {
  assert(x < std::numeric_limits<int>::max() / 2);
  int index;
  asm("bsrl %1, %0" : "=r"(index) : "r"(x << 1));
  return static_cast<size_t>(index);
}

/// Collect datapath stats if datapath stats are enabled
static inline void dpath_stat_inc(size_t *stat, size_t val = 1) {
  if (!kDatapathStats) {
    return;
  } else {
    *stat += val;
  }
}

}  // End ERpc

#endif

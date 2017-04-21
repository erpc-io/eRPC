#ifndef ERPC_COMMON_H
#define ERPC_COMMON_H

// Header file with convenience defines/functions that is included everywhere

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cerrno>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace ERpc {

// Debug defines
static constexpr bool kVerbose = true;  ///< Debug printing for non-datapath
static constexpr bool kDatapathVerbose = true;  ///< Debug printing in datapath
static constexpr bool kDatapathStats = true;  ///< Collect stats on the datapath

// Perf defines

/// Datapath checks that can be disabled for maximum performance
static constexpr bool kDatapathChecks = true;

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

#define _unused(x) ((void)(x))  // Make production build happy
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/// Collect datapath stats if datapath stats are enabled
static inline void dpath_stat_inc(size_t *stat, size_t val = 1) {
  if (!kDatapathStats) {
    return;
  } else {
    *stat += val;
  }
}

#define KB(x) (static_cast<size_t>(x) << 10)
#define KB_(x) (KB(x) - 1)
#define MB(x) (static_cast<size_t>(x) << 20)
#define MB_(x) (MB(x) - 1)

// General constants

/// The max Rpc ID. 256 threads per machine is enough.
static constexpr size_t kMaxRpcId = std::numeric_limits<uint8_t>::max() - 1;

/// The maximum number of machines in the cluster. This is only limited by
/// enet's maximum connections.
static constexpr size_t kMaxNumMachines = 4095;

static constexpr size_t kMaxBgThreads = 8;  ///< Max Nexus background threads
static constexpr size_t kMaxNumaNodes = 8;  ///< Maximum number of NUMA nodes
static constexpr size_t kPageSize = 4096;   ///< Page size in bytes
static constexpr size_t kHugepageSize = (2 * 1024 * 1024);  ///< Hugepage size
static constexpr size_t kMaxPhyPorts = 16;      ///< Max fabric device ports
static constexpr size_t kMaxHostnameLen = 128;  ///< Max hostname length
static constexpr size_t kMaxIssueMsgLen =  ///< Max debug issue message length
    (240 + kMaxHostnameLen * 2);           // Three lines and two hostnames

// Simple methods

/// Emulab hostnames are very long, so trim it to just the node name.
static std::string trim_hostname(std::string hostname) {
  if (hostname.find("emulab.net") != std::string::npos) {
    std::string trimmed_hostname = hostname.substr(0, hostname.find("."));
    return trimmed_hostname;
  } else {
    return hostname;
  }
}

}  // End ERpc

#endif

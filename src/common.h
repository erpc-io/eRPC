/**
 * @file common.h
 * @brief Header file with conveinence defines that is included everywhere
 */
#ifndef ERPC_COMMON_H
#define ERPC_COMMON_H

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
#include "tweakme.h"

namespace erpc {

#define _unused(x) ((void)(x))  // Make production build happy
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define KB(x) (static_cast<size_t>(x) << 10)
#define KB_(x) (KB(x) - 1)
#define MB(x) (static_cast<size_t>(x) << 20)
#define MB_(x) (MB(x) - 1)

// General constants

/// The max Rpc ID. 256 threads per machine is enough.
static constexpr size_t kMaxRpcId = UINT8_MAX - 1;

/// Array size to hold registered request handler functions
static constexpr size_t kReqTypeArraySize = 1ull + UINT8_MAX;

static constexpr size_t kMaxPhyPorts = 16;  ///< Max fabric device ports
static constexpr size_t kMaxBgThreads = 8;  ///< Max Nexus background threads
static constexpr size_t kMaxNumaNodes = 8;  ///< Maximum number of NUMA nodes
static constexpr size_t kHugepageSize = (2 * 1024 * 1024);  ///< Hugepage size
static constexpr size_t kMaxHostnameLen = 128;  ///< Max hostname length
static constexpr size_t kMaxIssueMsgLen =  ///< Max debug issue message length
    (240 + kMaxHostnameLen * 2);           // Three lines and two hostnames

// Invalid values
static constexpr uint8_t kInvalidNUMANode = 2;  ///< 0/1 only for now
static constexpr uint8_t kInvalidRpcId = kMaxRpcId + 1;
static constexpr uint8_t kInvalidReqType = kReqTypeArraySize - 1;
static constexpr uint8_t kInvalidPhyPort = kMaxPhyPorts + 1;
static constexpr uint8_t kInvalidSmUdpPort = 0;

/// Invalid eRPC thread ID of a background thread
static constexpr size_t kInvalidBgETid = kMaxBgThreads;

// Simple methods

/// Emulab hostnames are long, so trim it to just the node name.
static std::string trim_hostname(std::string hostname) {
  if (hostname.find("emulab.net") != std::string::npos) {
    std::string trimmed_hostname = hostname.substr(0, hostname.find("."));
    return trimmed_hostname;
  } else {
    return hostname;
  }
}

/// Check a condition at runtime. If the condition is false, throw exception.
static inline void rt_assert(bool condition, std::string throw_str) {
  if (unlikely(!condition)) throw std::runtime_error(throw_str);
}

/// Check a condition at runtime. If the condition is false, throw exception.
/// This is faster than rt_assert(cond, str) as it avoids string construction.
static inline void rt_assert(bool condition) {
  if (unlikely(!condition)) throw std::runtime_error("Error");
}

/// Check a condition at runtime. If the condition is false, print error message
/// and exit.
static inline void exit_assert(bool condition, std::string error_msg) {
  if (unlikely(!condition)) {
    fprintf(stderr, "%s. Exiting.\n", error_msg.c_str());
    fflush(stderr);
    exit(-1);
  }
}

static inline void dpath_stat_inc(size_t &stat, size_t val) {
  if (kDatapathStats) stat += val;
}

}  // End erpc

#endif

/**
 * @file common.h
 * @brief Common header file with convenience definitions
 */
#pragma once

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boost/algorithm/string.hpp>
#include <cerrno>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "config.h"
#include "rpc_constants.h"
#include "tweakme.h"

namespace erpc {

#define _unused(x) ((void)(x))  // Make production build happy
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define KB(x) (static_cast<size_t>(x) << 10)
#define MB(x) (static_cast<size_t>(x) << 20)
#define GB(x) (static_cast<size_t>(x) << 30)

#ifndef ERPC_TESTING
static constexpr bool kTesting = false;
#else
static constexpr bool kTesting = ERPC_TESTING;
#endif

// General constants

/// Array size to hold registered request handler functions
static constexpr size_t kReqTypeArraySize = 1ull + UINT8_MAX;

static constexpr size_t kHugepageSize = (2 * 1024 * 1024);  ///< Hugepage size
static constexpr size_t kMaxHostnameLen = 128;  ///< Max hostname length
static constexpr size_t kMaxIssueMsgLen =  ///< Max debug issue message length
    (240 + kMaxHostnameLen * 2);           // Three lines and two hostnames

// Invalid values
static constexpr uint8_t kInvalidRpcId = kMaxRpcId + 1;
static constexpr uint8_t kInvalidReqType = kReqTypeArraySize - 1;

/// Invalid eRPC thread ID of a background thread
static constexpr size_t kInvalidBgETid = kMaxBgThreads;

// Simple methods

/// Reduce hostname length for CloudLab/Emulab hostnames. For non-akalia users,
/// this returns the original hostname.
static std::string trim_hostname(const std::string hostname) {
  if (hostname.find("akalia") != std::string::npos) {
    std::vector<std::string> split;
    boost::split(split, hostname, boost::is_any_of("."));
    return split.at(0);
  }
  return hostname;
}

/// Check a condition at runtime. If the condition is false, throw exception.
static inline void rt_assert(bool condition, std::string throw_str, char *s) {
  if (unlikely(!condition)) {
    throw std::runtime_error(throw_str + std::string(s));
  }
}

/// Check a condition at runtime. If the condition is false, throw exception.
static inline void rt_assert(bool condition, const char *throw_str) {
  if (unlikely(!condition)) throw std::runtime_error(throw_str);
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
}  // namespace erpc

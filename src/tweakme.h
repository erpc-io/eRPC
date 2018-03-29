/**
 * @file tweakme.h
 * @brief Tweak this file to modify eRPC's behavior
 */
#ifndef ERPC_TWEAKME_H
#define ERPC_TWEAKME_H

#include <assert.h>
#include <stdlib.h>

namespace erpc {

class IBTransport;
class RawTransport;

// Congestion control
#define ENABLE_CC true

#if ENABLE_CC
static constexpr bool kCcRTT = true;       ///< Measure per-packet RTT
static constexpr bool kCcRateComp = true;  ///< Perform rate updates
static constexpr bool kCcPacing = true;    ///< Do packet pacing

static constexpr bool kCcOptBatchTsc = false;      ///< Use per-batch TSC
static constexpr bool kCcOptWheelBypass = false;   ///< Bypass wheel if safe
static constexpr bool kCcOptTimelyBypass = false;  ///< Bypass Timely if safe
#else
static constexpr bool kCcRTT = false;       ///< Measure per-packet RTT
static constexpr bool kCcRateComp = false;  ///< Perform rate updates
static constexpr bool kCcPacing = false;    ///< Do packet pacing

static constexpr bool kCcOptBatchTsc = true;      ///< Use per-batch TSC
static constexpr bool kCcOptWheelBypass = true;   ///< Bypass wheel if possible
static constexpr bool kCcOptTimelyBypass = true;  ///< Bypass Timely if possible
#endif

static_assert(kCcRTT || !kCcRateComp, "");  // Rate comp => RTT measurement

// Pick a transport. This is hard to control from CMake.

// 56 Gbps InfiniBand
// typedef IBTransport CTransport;
// static constexpr size_t kHeadroom = 0;
// static constexpr double kBandwidth = 7.0 * 1000 * 1000 * 1000;

// 40 Ethernet
typedef RawTransport CTransport;
static constexpr size_t kHeadroom = 40;
// static constexpr double kBandwidth = 5.0 * 1000 * 1000 * 1000;  // 40 Gbps
static constexpr double kBandwidth = 3.125 * 1000 * 1000 * 1000;  // 25 Gbps

#if defined(TESTING)
static constexpr bool kTesting = true;
#else
static constexpr bool kTesting = false;
#endif

static constexpr bool kDatapathStats = false;
}

#endif  // ERPC_CONFIG_H

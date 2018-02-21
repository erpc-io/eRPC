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
static constexpr bool kCcRTT = false;       ///< Measure per-packet RTT
static constexpr bool kCcRateComp = false;  ///< Perform rate updates
static constexpr bool kCcPacing = false;    ///< Do packet pacing
static_assert(kCcRTT || !kCcRateComp, "");  // Rate comp => RTT measurement

static constexpr bool kCcOptBatchTsc = true;      ///< Use per-batch TSC
static constexpr bool kCcOptWheelBypass = true;   ///< Bypass wheel if possible
static constexpr bool kCcOptTimelyBypass = true;  ///< Bypass Timely if possible

// Pick a transport. This is hard to control from CMake.

// 56 Gbps InfiniBand
// static constexpr size_t kHeadroom = 0;
// static constexpr double kBandwidth = 7.0 * 1000 * 1000 * 1000;
// typedef IBTransport CTransport;

// 40 Gbps Ethernet
static constexpr size_t kHeadroom = 40;
static constexpr double kBandwidth = 5.0 * 1000 * 1000 * 1000;
typedef RawTransport CTransport;

#if defined(TESTING)
static constexpr bool kTesting = true;
#else
static constexpr bool kTesting = false;
#endif

static constexpr bool kDatapathStats = false;
}

#endif  // ERPC_CONFIG_H

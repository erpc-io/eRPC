/**
 * @file tweakme.h
 * @brief Tweak this file to modify eRPC's behavior
 */
#ifndef ERPC_TWEAKME_H
#define ERPC_TWEAKME_H

#include <assert.h>
#include <stdlib.h>

namespace erpc {

/// Packet loss timeout for an RPC request in microseconds
static constexpr size_t kRpcRTOUs = 5000;

// Congestion control
#define ENABLE_CC true
#define ENABLE_CC_OPTS false

static constexpr bool kCcRTT = ENABLE_CC;       ///< Measure per-packet RTT
static constexpr bool kCcRateComp = ENABLE_CC;  ///< Perform rate computation
static constexpr bool kCcPacing = ENABLE_CC;    ///< Use rate limiter for pacing

/// Sample RDTSC once per RX/TX batch for RTT measurements
static constexpr bool kCcOptBatchTsc = ENABLE_CC_OPTS;

/// Bypass timing wheel if a session is uncongested
static constexpr bool kCcOptWheelBypass = ENABLE_CC_OPTS;

/// Bypass Timely rate update if session is uncongested and RTT is below T_low
static constexpr bool kCcOptTimelyBypass = ENABLE_CC_OPTS;

static_assert(kCcRTT || !kCcRateComp, "");  // Rate comp => RTT measurement

// Pick a transport. This is hard to control from CMake.
class IBTransport;
class RawTransport;

// 56 Gbps InfiniBand
// typedef IBTransport CTransport;
// static constexpr size_t kHeadroom = 0;
// static constexpr double kBandwidth = 7.0 * 1000 * 1000 * 1000;

// 40 Ethernet
typedef RawTransport CTransport;
static constexpr size_t kHeadroom = 40;
// static constexpr double kBandwidth = 5.0 * 1000 * 1000 * 1000;  // 40 Gbps
static constexpr double kBandwidth = 3.125 * 1000 * 1000 * 1000;  // 25 Gbps

static constexpr bool kDatapathStats = false;
}

#endif  // ERPC_CONFIG_H

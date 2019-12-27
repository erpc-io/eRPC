/**
 * @file tweakme.h
 * @brief Tweak this file to modify eRPC's behavior
 */
#pragma once

#include <assert.h>
#include <stdlib.h>

namespace erpc {

/// Packet loss timeout for an RPC request in microseconds
static constexpr size_t kRpcRTOUs = 5000;

// Congestion control
static constexpr bool kEnableCc = true;
static constexpr bool kEnableCcOpts = true;

static constexpr bool kCcRTT = kEnableCc;       ///< Measure per-packet RTT
static constexpr bool kCcRateComp = kEnableCc;  ///< Perform rate computation
static constexpr bool kCcPacing = kEnableCc;    ///< Use rate limiter for pacing

/// Sample RDTSC once per RX/TX batch for RTT measurements
static constexpr bool kCcOptBatchTsc = kEnableCcOpts;

/// Bypass timing wheel if a session is uncongested
static constexpr bool kCcOptWheelBypass = kEnableCcOpts;

/// Bypass Timely rate update if session is uncongested and RTT is below T_low
static constexpr bool kCcOptTimelyBypass = kEnableCcOpts;

static_assert(kCcRTT || !kCcRateComp, "");  // Rate comp => RTT measurement

/// Invoke request handlers directly on RX ring buffers to avoid copying
/// to a dynamically-allocated msgbuf. Enabling this optimization restricts
/// ownership of single-packet request msgbufs at the server to the duration
/// of the request handler.
static constexpr bool kZeroCopyRX = true;

static constexpr bool kDatapathStats = false;
}  // namespace erpc

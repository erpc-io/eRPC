/**
 * @file dpdk_externs.h
 * @brief Externs for DPDK transport implementation
 */
#pragma once

#ifdef ERPC_DPDK

#include <atomic>
#include "common.h"

namespace erpc {

extern std::mutex g_dpdk_lock;
extern volatile bool g_dpdk_initialized;

}  // namespace erpc

#endif

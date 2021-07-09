/**
 * @file dpdk_externs.h
 *
 * @brief Externs for DPDK transport implementation. These globals are shared
 * among different DpdkTransport objects
 */
#pragma once

#ifdef ERPC_DPDK

#include "dpdk_transport.h"

#include <atomic>
#include <set>
#include "common.h"

namespace erpc {

extern std::mutex g_dpdk_lock;
extern bool g_dpdk_initialized;
extern bool g_port_initialized[RTE_MAX_ETHPORTS];

/// If the DPDK management daemon exists, this is a pointer to the memzone
/// created by the daemon. Else, it's a normal heap pointer.
extern DpdkTransport::ownership_memzone_t *g_memzone;
}  // namespace erpc

#endif

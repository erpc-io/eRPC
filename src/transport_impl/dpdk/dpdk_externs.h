/**
 * @file dpdk_externs.h
 *
 * @brief Externs for DPDK transport implementation. These globals are shared
 * among different DpdkTransport objects
 */
#pragma once

#ifdef ERPC_DPDK

#include <rte_common.h>
#include <rte_mempool.h>
#include <atomic>
#include <set>
#include "common.h"
#include "dpdk_transport.h"

namespace erpc {

extern std::mutex g_dpdk_lock;
extern bool g_dpdk_initialized;
extern bool g_port_initialized[RTE_MAX_ETHPORTS];

/// If the DPDK management daemon exists, this is a pointer to the memzone
/// created by the daemon. Else, it's a normal heap pointer.
extern DpdkTransport::ownership_memzone_t *g_memzone;
}  // namespace erpc

#endif

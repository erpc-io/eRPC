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

extern DpdkTransport::DpdkProcType g_dpdk_proc_type;

/// A pointer to the memzone created by the eRPC DPDK daemon, if the daemon
/// exists
extern DpdkTransport::memzone_contents_t *g_memzone;

/// The set of queue IDs in use by Rpc objects in this process
extern std::set<size_t> g_used_qp_ids[RTE_MAX_ETHPORTS];

/// g_mempool_arr[i][j] is the mempool to use for port i, queue j
extern rte_mempool
    *g_mempool_arr[RTE_MAX_ETHPORTS][DpdkTransport::kMaxQueuesPerPort];

}  // namespace erpc

#endif

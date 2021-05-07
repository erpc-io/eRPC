#ifdef ERPC_DPDK

#include "dpdk_externs.h"
#include "dpdk_transport.h"

namespace erpc {

std::mutex g_dpdk_lock;
volatile bool g_dpdk_initialized(false);
volatile bool g_port_initialized[RTE_MAX_ETHPORTS];
std::set<size_t> g_used_qp_ids[RTE_MAX_ETHPORTS];
rte_mempool *g_mempool_arr[RTE_MAX_ETHPORTS][DpdkTransport::kMaxQueuesPerPort];

}  // namespace erpc

#endif

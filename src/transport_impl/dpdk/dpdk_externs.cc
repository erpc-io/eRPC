#ifdef ERPC_DPDK

#include "dpdk_externs.h"
#include "dpdk_transport.h"

namespace erpc {

std::mutex g_dpdk_lock;
bool g_dpdk_initialized;
bool g_port_initialized[RTE_MAX_ETHPORTS];
DpdkTransport::ownership_memzone_t *g_memzone;

}  // namespace erpc

#endif

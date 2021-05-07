#ifdef ERPC_DPDK

#include "dpdk_transport.h"
#include "dpdk_externs.h"

namespace erpc {

std::mutex g_dpdk_lock;
volatile bool g_dpdk_initialized(false);

}  // namespace erpc

#endif

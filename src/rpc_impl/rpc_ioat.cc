#include <rte_ioat_rawdev.h>
#include <rte_rawdev.h>
#include "rpc.h"
#include "util/externs.h"

namespace erpc {

template <class TTr>
void Rpc<TTr>::setup_ioat() {
  static constexpr size_t kIoatRingSize = 512;
  _unused(kIoatRingSize);

  dpdk_lock.lock();
  if (!dpdk_initialized) {
    ERPC_INFO("I/OAT setup for Rpc %u initializing DPDK\n", rpc_id);

    // RTE_LOG_WARNING is log level 5
    const char *rte_argv[] = {"-c", "1",  "-n",  "4", "--log-level",
                              "5",  "-m", "128", NULL};

    int rte_argc = sizeof(rte_argv) / sizeof(rte_argv[0]) - 1;
    int ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
    _unused(ret);
  } else {
    ERPC_INFO("I/OAT setup for Rpc %u skipping DPDK initialization\n", rpc_id);
  }

  dpdk_lock.unlock();
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

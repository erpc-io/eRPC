#include "dpdk_externs.h"
#include "dpdk_transport.h"

namespace erpc {

static constexpr size_t kPhyPort = 0;

int main() {
  ERPC_WARN("eRPC DPDK daemon: Welcome!");

  // n: channels, m: maximum memory in megabytes
  const char *rte_argv[] = {
      "--proc-type", "primary",                                            //
      "-c",          "1",                                                  //
      "-n",          "6",                                                  //
      "--log-level", (ERPC_LOG_LEVEL >= ERPC_LOG_LEVEL_INFO) ? "8" : "0",  //
      "-m",          "1024",                                               //
      nullptr};

  int rte_argc = static_cast<int>(sizeof(rte_argv) / sizeof(rte_argv[0])) - 1;
  int ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
  rt_assert(ret >= 0, "Failed to initialize DPDK");
  return 0;
}

}  // namespace erpc

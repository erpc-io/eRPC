#include <rte_rawdev.h>
// Newline to prevent reordering rte_ioat_rawdev.h before rte_rawdev.h
#include <rte_ioat_rawdev.h>

#include "rpc.h"
#include "util/externs.h"

namespace erpc {

/// The set of I/OAT dev IDs in use by Rpc objects in this process. Uses
/// dpdk_lock.
static std::set<size_t> used_rawdev_ids;

template <class TTr>
void Rpc<TTr>::setup_ioat() {
  static constexpr size_t kIoatRingSize = 512;
  _unused(kIoatRingSize);

  dpdk_lock.lock();

  if (!dpdk_initialized) {
    ERPC_INFO("I/OAT setup for Rpc %u initializing DPDK\n", rpc_id);

    // RTE_LOG_WARNING is log level 5
    const char *rte_argv[] = {"-c", "1",  "-n",  "6", "--log-level",
                              "0",  "-m", "128", NULL};

    int rte_argc = sizeof(rte_argv) / sizeof(rte_argv[0]) - 1;
    int ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
    rt_assert(ret >= 0, "Failed to initialize DPDK");

    dpdk_initialized = true;
  } else {
    ERPC_INFO("I/OAT setup for Rpc %u skipping DPDK initialization\n", rpc_id);
  }

  size_t rawdev_count = rte_rawdev_count();
  rt_assert(rawdev_count >= 1, "No I/OAT devices online");

  ioat_dev_id = SIZE_MAX;
  for (size_t i = 0; i < rawdev_count; i++) {
    if (used_rawdev_ids.count(i) == 0) {
      ioat_dev_id = i;
      used_rawdev_ids.insert(i);
      break;
    }
  }

  rt_assert(ioat_dev_id != SIZE_MAX, "All I/OAT devices in use");

  ERPC_INFO("Rpc %u using I/OAT device %zu\n", rpc_id, ioat_dev_id);

  struct rte_rawdev_info info;
  info.dev_private = NULL;

  rt_assert(rte_rawdev_info_get(ioat_dev_id, &info) == 0);
  rt_assert(std::string(info.driver_name).find("ioat") != std::string::npos);

  struct rte_ioat_rawdev_config p;
  memset(&info, 0, sizeof(info));
  info.dev_private = &p;

  rte_rawdev_info_get(ioat_dev_id, &info);
  rt_assert(p.ring_size == 0, "Initial ring size is non-zero");

  p.ring_size = kIoatRingSize;
  rt_assert(rte_rawdev_configure(ioat_dev_id, &info) == 0,
            "rte_rawdev_configure failed");

  rte_rawdev_info_get(ioat_dev_id, &info);
  rt_assert(p.ring_size == kIoatRingSize, "Wrong ring size");

  rt_assert(rte_rawdev_start(ioat_dev_id) == 0, "Rawdev start failed");

  dpdk_lock.unlock();
}

FORCE_COMPILE_TRANSPORTS

}  // namespace erpc

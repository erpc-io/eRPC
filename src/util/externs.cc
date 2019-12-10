#include "externs.h"
#include <atomic>

namespace erpc {

std::mutex dpdk_lock;
volatile bool dpdk_initialized(false);

}  // namespace erpc

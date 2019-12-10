#pragma once

#include <atomic>
#include "common.h"

namespace erpc {

extern std::mutex dpdk_lock;
extern volatile bool dpdk_initialized;

}  // namespace erpc

#pragma once

#include <atomic>
#include "common.h"

namespace erpc {

std::mutex dpdk_lock;
volatile bool dpdk_initialized(false);

}  // namespace erpc

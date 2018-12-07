/**
 * @file rpc_constants.h
 * @brief eRPC constants that are of interest to the user
 */
#pragma once
#include <stdint.h>
#include <stdlib.h>

namespace erpc {
/**
 * @relates Rpc
 * @brief Maximum number of eRPC processes per machine
 */
static constexpr size_t kMaxNumERpcProcesses = 256;

/**
 * @relates Rpc
 * @brief The management port for an eRPC processes must be between
 * \p kBaseSmUdpPort and (\p kBaseSmUdpPort + \p kMaxNumERpcProcesses)
 */
static constexpr uint16_t kBaseSmUdpPort = 31850;

/**
 * @relates Rpc
 * @brief Maximum number of NUMA nodes per machine
 */
static constexpr size_t kMaxNumaNodes = 8;

/**
 * @relates Rpc
 * @brief Maximum number of background threads per process
 */
static constexpr size_t kMaxBgThreads = 8;

/**
 * @relates Rpc
 * @brief Maximum number of datapath device ports
 */
static constexpr size_t kMaxPhyPorts = 16;
};

/**
 * @file misc.h
 * @brief Misc utility functions
 */

#ifndef ERPC_MISC_H
#define ERPC_MISC_H

#include <stdio.h>
#include <thread>

namespace ERpc {

/// Bind thread to core n
static void bind_to_core(std::thread &thread, size_t n) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(n, &cpuset);
  int rc = pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t),
                                  &cpuset);
  if (rc != 0) {
    throw std::runtime_error("Error setting thread affinity.\n");
  }
}

}  // End ERpc

#endif

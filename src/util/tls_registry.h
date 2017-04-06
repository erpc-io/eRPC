#ifndef ERPC_TLS_REGISTRY_H
#define ERPC_TLS_REGISTRY_H

#include <atomic>
#include "common.h"

namespace ERpc {

class TlsRegistry {
 public:
  TlsRegistry() : cur_tiny_tid(0) {}
  std::atomic<size_t> cur_tiny_tid;

  /// Initialize all the thread-local registry members
  void init();

  /// Return the tiny thread ID of the caller
  size_t get_tiny_tid() const;
};  // End ERpc
}

#endif

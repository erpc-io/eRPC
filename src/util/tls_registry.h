#ifndef ERPC_TLS_REGISTRY_H
#define ERPC_TLS_REGISTRY_H

#include <atomic>
#include "common.h"

namespace ERpc {

class TlsRegistry {
 public:
  TlsRegistry() : cur_tiny_tid(0) {}
  std::atomic<size_t> cur_tiny_tid;

  void init();
  size_t get_tls_tiny_tid();
};  // End ERpc
}

#endif

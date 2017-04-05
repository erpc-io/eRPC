#include "tls_registry.h"
#include "common.h"

namespace ERpc {

thread_local bool tls_initalized;
thread_local size_t tls_tiny_tid;

void TlsRegistry::init() {
  assert(!tls_initalized);
  tls_initalized = true;
  tls_tiny_tid = cur_tiny_tid++;
}

size_t TlsRegistry::get_tls_tiny_tid() {
  assert(tls_initalized);
  return tls_tiny_tid;
}

}  // End ERpc

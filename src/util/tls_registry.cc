#include "tls_registry.h"
#include "common.h"

namespace ERpc {

thread_local bool tls_initalized;
thread_local size_t tiny_tid;

void TlsRegistry::init() {
  assert(!tls_initalized);
  tls_initalized = true;
  tiny_tid = cur_tiny_tid++;
}

size_t TlsRegistry::get_tiny_tid() const {
  assert(tls_initalized);
  return tiny_tid;
}

}  // End ERpc

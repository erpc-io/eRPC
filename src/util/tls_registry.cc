#include "tls_registry.h"
#include "common.h"

namespace erpc {

thread_local bool tls_initialized;
thread_local size_t etid;

void TlsRegistry::init() {
  assert(!tls_initialized);
  tls_initialized = true;
  etid = cur_etid++;
}

size_t TlsRegistry::get_etid() const {
  assert(tls_initialized);
  return etid;
}

}  // End erpc

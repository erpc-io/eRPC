#include "tls_registry.h"
#include "common.h"

namespace erpc {

thread_local bool tls_initialized = false;
thread_local size_t etid = SIZE_MAX;

void TlsRegistry::init() {
  assert(!tls_initialized);
  tls_initialized = true;
  etid = cur_etid_++;
}

void TlsRegistry::reset() {
  tls_initialized = false;
  etid = SIZE_MAX;
  cur_etid_ = 0;
}

size_t TlsRegistry::get_etid() const {
  assert(tls_initialized);
  return etid;
}

}  // namespace erpc

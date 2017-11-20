#include "tls_registry.h"
#include "common.h"

namespace erpc {

thread_local bool tls_initialized = false;
thread_local size_t etid = std::numeric_limits<size_t>::max();

void TlsRegistry::init() {
  assert(!tls_initialized);
  tls_initialized = true;
  etid = cur_etid++;
}

void TlsRegistry::reset() {
  tls_initialized = false;
  etid = std::numeric_limits<size_t>::max();
  cur_etid = 0;
}

size_t TlsRegistry::get_etid() const {
  assert(tls_initialized);
  return etid;
}

}  // End erpc

#include "tls_registry.h"
#include "common.h"

namespace ERpc {

thread_local bool tls_initalized;
thread_local size_t etid;

void TlsRegistry::init() {
  assert(!tls_initalized);
  tls_initalized = true;
  etid = cur_etid++;
}

size_t TlsRegistry::get_etid() const {
  assert(tls_initalized);
  return etid;
}

}  // End ERpc

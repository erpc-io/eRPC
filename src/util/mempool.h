#ifndef ERPC_MEMPOOL_H
#define ERPC_MEMPOOL_H

#include "common.h"
#include "util/huge_alloc.h"

namespace erpc {

/// A hugepage-backed pool for objects of type T. Objects are not registered
/// with the NIC.
template <class T>
class MemPool {
  size_t num_to_alloc = MB(2) / sizeof(T);  // Start with 2 MB
  HugeAlloc *huge_alloc;
  std::vector<T *> pool;

  void extend_pool() {
    size_t alloc_sz = sizeof(T) * num_to_alloc;

    // alloc_raw()'s result is leaked
    Buffer b = huge_alloc->alloc_raw(alloc_sz, DoRegister::kFalse);
    rt_assert(b.buf != nullptr, "Hugepage allocation failed");

    for (size_t i = 0; i < num_to_alloc; i++) {
      pool.push_back(&b.buf[sizeof(T) * i]);
    }
    num_to_alloc *= 2;
  }

  T *alloc() {
    if (pool.empty()) extend_pool();
    T *ret = pool.back();
    pool.pop_back();
    return ret;
  }

  void free(T *t) { pool.push_back(t); }

  MemPool(HugeAlloc *huge_alloc) : huge_alloc(huge_alloc) {}

  /// Cleanup is done when owner deletes huge_alloc
  ~MemPool() {}
};
}

#endif  // ERPC_MEMPOOL_H

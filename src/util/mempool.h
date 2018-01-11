#ifndef ERPC_MEMPOOL_H
#define ERPC_MEMPOOL_H

#include "common.h"
#include "util/huge_alloc.h"

namespace erpc {

/// A hugepage-backed pool for objects of type T
template <class T>
class MemPool {
  size_t num_to_alloc = 1024;
  HugeAlloc *huge_alloc;
  std::vector<T *> pool;

  void extend_pool() {
    size_t alloc_sz = sizeof(T) * num_to_alloc;
    alloc_sz = std::max(alloc_sz, HugeAlloc::kMaxClassSize);

    Buffer b = huge_alloc->alloc(alloc_sz);
    rt_assert(b.buf != nullptr, "Hugepage allocation failed");

    for (size_t i = 0; i < num_to_alloc; i++) {
      pool.push_back(&backing_ptr[i]);
    }
    backing_ptr_vec.push_back(backing_ptr);
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

  ~MemPool() {
    for (Buffer &b : backing_buffer_vec) huge_alloc->free_buf(b);
  }
};
}

#endif  // ERPC_MEMPOOL_H

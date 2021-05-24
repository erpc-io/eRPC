#pragma once

#include "common.h"
#include "util/huge_alloc.h"

namespace erpc {

/// A hugepage-backed pool for objects of type T. Objects are not registered
/// with the NIC.
template <class T>
class MemPool {
  size_t num_to_alloc_ = MB(2) / sizeof(T);  // Start with 2 MB
  HugeAlloc *huge_alloc_;
  std::vector<T *> pool_;

  void extend_pool() {
    size_t alloc_sz = sizeof(T) * num_to_alloc_;

    // alloc_raw()'s result is leaked
    Buffer b = huge_alloc_->alloc_raw(alloc_sz, DoRegister::kFalse);
    rt_assert(b.buf_ != nullptr, "Hugepage allocation failed");

    for (size_t i = 0; i < num_to_alloc_; i++) {
      pool_.push_back(reinterpret_cast<T *>(&b.buf_[sizeof(T) * i]));
    }
    num_to_alloc_ *= 2;
  }

 public:
  T *alloc() {
    if (pool_.empty()) extend_pool();
    T *ret = pool_.back();
    pool_.pop_back();
    return ret;
  }

  void free(T *t) { pool_.push_back(t); }

  MemPool(HugeAlloc *huge_alloc) : huge_alloc_(huge_alloc) {}

  /// Cleanup is done when owner deletes huge_alloc
  ~MemPool() {}
};
}  // namespace erpc

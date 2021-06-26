#pragma once

#include <assert.h>
#include "common.h"

namespace erpc {

/**
 * @brief A fast static-sized vector that supports push_back, pop_back, and
 * random access. Exceeding the size of the vector results in an error.
 *
 * @tparam T The type of elements stored in the vector
 * @tparam N The size of the vector
 */
template <typename T, size_t N>
class FixedVector {
 public:
  FixedVector() {}
  ~FixedVector() {}

  inline void push_back(T t) {
    assert(free_index_ < N);  // \p free_index can be up to N - 1
    arr_[free_index_] = t;
    free_index_++;
  }

  inline T pop_back() {
    assert(free_index_ > 0);      // If free_index is 0, there is nothing to pop
    T t = arr_[free_index_ - 1];  // The slot at free_index - 1 is occupied
    free_index_--;

    return t;
  }

  /// Return the number of elements currently in the vector
  inline size_t size() { return free_index_; }

  /// Return the maximum capacity of the FixedVector
  inline size_t capacity() { return N; }

  inline T operator[](size_t i) {
    assert(i < free_index_);  // There is no element at \p free_index
    return arr_[i];
  }

  T arr_[N];               // N is the capacity of the vector
  size_t free_index_ = 0;  // Index of the first free slot
};

}  // namespace erpc

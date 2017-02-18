#ifndef FIXED_VECTOR_H
#define FIXED_VECTOR_H

#include <assert.h>

namespace ERpc {

template <typename T, int N>
class trivial_vector {
 public:
  trivial_vector() {}
  ~trivial_vector() {}

  inline void push_back(T t) {
    assert(free_index < N); /* \p free_index can be up to N - 1 */
    arr[free_index] = t;
    free_index++;
  }

  inline T pop_back() {
    assert(free_index > 0) /* If free_index is 0, there is nothing to pop */
        T t = arr[free_index - 1]; /* The slot at free_index - 1 is occupied */
    free_index--;

    return t;
  }

  inline T operator[](size_t i) {
    assert(i < free_index); /* There is no element at \p free_index */
    return arr[i];
  }

  T arr[N];              /* N is the capacity of the vector */
  size_t free_index = 0; /* Index of the first free slot */
};

}  // End ERpc

#endif /* FIXED_VECTOR_H */

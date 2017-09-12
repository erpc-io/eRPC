#pragma once
#ifndef MICA_UTIL_SAFE_CAST_H_
#define MICA_UTIL_SAFE_CAST_H_

#include <cassert>
#include <string>
#include <cstdio>
#include "mica/common.h"
#define NEEDS_NULLPTR_DEFINED 0
#include "mica/util/SafeInt/SafeInt3_mod.hpp"
#undef NEEDS_NULLPTR_DEFINED

namespace mica {
namespace util {
template <class To, class From>
static To safe_cast(From x) noexcept {
  try {
    return static_cast<To>(SafeInt<To>(x));
  } catch (...) {
    fprintf(stderr,
            "error: unable to cast safely due to an out-of-range error\n");
    assert(false);
    return To(0);
  }
  // return static_cast<To>(x);
}
}
}

#endif
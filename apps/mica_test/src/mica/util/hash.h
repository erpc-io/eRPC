#pragma once
#ifndef MICA_UTIL_HASH_H_
#define MICA_UTIL_HASH_H_

#include "mica/common.h"
#include "mica/util/cityhash/citycrc_mod.h"

namespace mica {
namespace util {
template <typename T>
static uint64_t hash(const T* key, size_t len) {
  return CityHash64(reinterpret_cast<const char*>(key), len);
}
}
}

#endif
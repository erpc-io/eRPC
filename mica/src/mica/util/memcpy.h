#pragma once
#ifndef MICA_UTIL_MEMCPY_H_
#define MICA_UTIL_MEMCPY_H_

#include <cstring>
#include "mica/common.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include "mica/util/rte_memcpy/rte_memcpy_mod.h"

#include "mica/util/roundup.h"

namespace mica {
namespace util {
static void memset(void* dest, int c, size_t n) { ::memset(dest, c, n); }

static void memcpy(void* dest, const void* src, size_t n) {
  ::mica::util::rte_memcpy_func(dest, src, n);
}

static void memmove(void* dest, const void* src, size_t n) {
  ::memmove(dest, src, n);
}

static int memcmp(const void* a, const void* b, size_t n) {
  return ::memcmp(a, b, n);
}

static bool memcmp_equal(const void* a, const void* b, size_t n) {
  return ::memcmp(a, b, n) == 0;
}

// Wrappers for any pointer.
template <typename T>
static void memset(T* dest, int c, size_t n) {
  ::mica::util::memset(reinterpret_cast<void*>(dest), c, n);
}

template <typename T1, typename T2>
static void memcpy(T1* dest, const T2* src, size_t n) {
  ::mica::util::memcpy(reinterpret_cast<void*>(dest),
                       reinterpret_cast<const void*>(src), n);
}

template <typename T1, typename T2>
static void memmove(T1* dest, const T2* src, size_t n) {
  ::mica::util::memmove(reinterpret_cast<void*>(dest),
                        reinterpret_cast<const void*>(src), n);
}

template <typename T1, typename T2>
static int memcmp(const T1* a, const T2* b, size_t n) {
  return ::mica::util::memcmp(reinterpret_cast<const void*>(a),
                              reinterpret_cast<const void*>(b), n);
}

template <typename T1, typename T2>
static bool memcmp_equal(const T1* a, const T2* b, size_t n) {
  return ::mica::util::memcmp_equal(reinterpret_cast<const void*>(a),
                                    reinterpret_cast<const void*>(b), n);
}

template <int Alignment, typename T>
static void memset(T* dest, int c, size_t n) {
  assert(n == ::mica::util::roundup<Alignment>(n));
  assert(reinterpret_cast<size_t>(dest) % Alignment == 0);
  ::mica::util::memset(dest, c, n);
}

template <int Alignment, typename T1, typename T2>
static void memcpy(T1* dest, const T2* src, size_t n) {
  assert(n == ::mica::util::roundup<Alignment>(n));
  assert(reinterpret_cast<size_t>(dest) % Alignment == 0);
  assert(reinterpret_cast<size_t>(src) % Alignment == 0);
  if (Alignment == 8) {
    switch (n >> 3) {
      case 4:
        *(uint64_t*)(reinterpret_cast<char*>(dest) + 24) =
            *(const uint64_t*)(reinterpret_cast<const char*>(src) + 24);
      // fall-through
      case 3:
        *(uint64_t*)(reinterpret_cast<char*>(dest) + 16) =
            *(const uint64_t*)(reinterpret_cast<const char*>(src) + 16);
      // fall-through
      case 2:
        *(uint64_t*)(reinterpret_cast<char*>(dest) + 8) =
            *(const uint64_t*)(reinterpret_cast<const char*>(src) + 8);
      // fall-through
      case 1:
        *(uint64_t*)(reinterpret_cast<char*>(dest) + 0) =
            *(const uint64_t*)(reinterpret_cast<const char*>(src) + 0);
      // fall-through
      case 0:
        break;
      default:
        ::mica::util::memcpy(dest, src, n);
        break;
    }
  } else
    ::mica::util::memcpy(dest, src, n);
}

template <int Alignment, typename T1, typename T2>
static void memmove(T1* dest, const T2* src, size_t n) {
  assert(n == ::mica::util::roundup<Alignment>(n));
  assert(reinterpret_cast<size_t>(dest) % Alignment == 0);
  assert(reinterpret_cast<size_t>(src) % Alignment == 0);
  ::mica::util::memmove(dest, src, n);
}

template <int Alignment, typename T1, typename T2>
static int memcmp(const T1* a, const T2* b, size_t n) {
  assert(n == ::mica::util::roundup<Alignment>(n));
  assert(reinterpret_cast<size_t>(a) % Alignment == 0);
  assert(reinterpret_cast<size_t>(b) % Alignment == 0);
  return ::mica::util::memcmp(a, b, n);
}

template <int Alignment, typename T1, typename T2>
static bool memcmp_equal(const T1* a, const T2* b, size_t n) {
  assert(n == ::mica::util::roundup<Alignment>(n));
  assert(reinterpret_cast<size_t>(a) % Alignment == 0);
  assert(reinterpret_cast<size_t>(b) % Alignment == 0);
  // printf("%p %p %zu\n", a, b, n);
  if (Alignment == 8) {
    switch (n >> 3) {
      case 4:
        if (*reinterpret_cast<const uint64_t*>(
                reinterpret_cast<const char*>(a) + 24) !=
            *reinterpret_cast<const uint64_t*>(
                reinterpret_cast<const char*>(b) + 24))
          return false;
      // fall-through
      case 3:
        if (*reinterpret_cast<const uint64_t*>(
                reinterpret_cast<const char*>(a) + 16) !=
            *reinterpret_cast<const uint64_t*>(
                reinterpret_cast<const char*>(b) + 16))
          return false;
      // fall-through
      case 2:
        if (*reinterpret_cast<const uint64_t*>(
                reinterpret_cast<const char*>(a) + 8) !=
            *reinterpret_cast<const uint64_t*>(
                reinterpret_cast<const char*>(b) + 8))
          return false;
      // fall-through
      case 1:
        if (*reinterpret_cast<const uint64_t*>(
                reinterpret_cast<const char*>(a) + 0) !=
            *reinterpret_cast<const uint64_t*>(
                reinterpret_cast<const char*>(b) + 0))
          return false;
      // fall-through
      case 0:
        return true;
      default:
        return ::mica::util::memcmp_equal(a, b, n);
    }
  } else
    return ::mica::util::memcmp_equal(a, b, n);
}
}
}

#pragma GCC diagnostic pop
#endif

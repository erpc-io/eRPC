/* Masstree
 * Eddie Kohler, Yandong Mao, Robert Morris
 * Copyright (c) 2012-2013 President and Fellows of Harvard College
 * Copyright (c) 2012-2013 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Masstree LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Masstree LICENSE file; the license in that file
 * is legally binding.
 */
#ifndef CLICK_HASHCODE_HH
#define CLICK_HASHCODE_HH
#include <stddef.h>
#include <inttypes.h>
#if HAVE_STD_HASH
#include <functional>
#endif

// Notes about the hashcode template: On GCC 4.3.0, "template <>" is required
// on the specializations or they aren't used.  Just plain overloaded
// functions aren't used.  The specializations must be e.g. "const char &",
// not "char", or GCC complains about a specialization not matching the
// general template.  The main template takes a const reference for two
// reasons.  First, providing both "hashcode_t hashcode(T)" and "hashcode_t
// hashcode(const T&)" leads to ambiguity errors.  Second, providing only
// "hashcode_t hashcode(T)" is slower by looks like 8% when T is a String,
// because of copy constructors; for types with more expensive non-default
// copy constructors this would probably be worse.

typedef size_t hashcode_t;	///< Typical type for a hashcode() value.

template <typename T>
inline hashcode_t hashcode(T const &x) {
    return x.hashcode();
}

template <>
inline hashcode_t hashcode(char const &x) {
    return x;
}

template <>
inline hashcode_t hashcode(signed char const &x) {
    return x;
}

template <>
inline hashcode_t hashcode(unsigned char const &x) {
    return x;
}

template <>
inline hashcode_t hashcode(short const &x) {
    return x;
}

template <>
inline hashcode_t hashcode(unsigned short const &x) {
    return x;
}

template <>
inline hashcode_t hashcode(int const &x) {
    return x;
}

template <>
inline hashcode_t hashcode(unsigned const &x) {
    return x;
}

template <>
inline hashcode_t hashcode(long const &x) {
    return x;
}

template <>
inline hashcode_t hashcode(unsigned long const &x) {
    return x;
}

template <>
inline hashcode_t hashcode(long long const &x) {
    return (x >> 32) ^ x;
}

template <>
inline hashcode_t hashcode(unsigned long long const &x) {
    return (x >> 32) ^ x;
}

#if HAVE_INT64_TYPES && !HAVE_INT64_IS_LONG && !HAVE_INT64_IS_LONG_LONG
template <>
inline hashcode_t hashcode(int64_t const &x) {
    return (x >> 32) ^ x;
}

template <>
inline hashcode_t hashcode(uint64_t const &x) {
    return (x >> 32) ^ x;
}
#endif

template <typename T>
inline hashcode_t hashcode(T * const &x) {
    return reinterpret_cast<uintptr_t>(x) >> 3;
}

template <typename T>
inline typename T::key_const_reference hashkey(const T &x) {
    return x.hashkey();
}

#endif

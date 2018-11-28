/* Masstree
 * Eddie Kohler, Yandong Mao, Robert Morris
 * Copyright (c) 2012-2014 President and Fellows of Harvard College
 * Copyright (c) 2012-2014 Massachusetts Institute of Technology
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
#ifndef STRING_SLICE_HH
#define STRING_SLICE_HH 1
#include "str.hh"
#include <algorithm>
#include <assert.h>
#include <string.h>
class threadinfo;

/** @brief Provide access to T-typed slices of a string. */
template <typename T> struct string_slice {
  private:
    union union_type {
        T x;
        char s[sizeof(T)];
        union_type(T x)
            : x(x) {
        }
    };

  public:
    typedef T type;

    /** @brief Size of T in bytes. */
    static constexpr int size = (int) sizeof(T);

    /** @brief Return a T containing data from a string's prefix. */
    static T make(const char *s, int len) {
        if (len <= 0)
            return 0;
#if HAVE_UNALIGNED_ACCESS
        if (len >= size)
            return *reinterpret_cast<const T *>(s);
#endif
        union_type u(0);
        memcpy(u.s, s, std::min(len, size));
        return u.x;
    }

    /** @brief Return a T that compares similarly to a string's prefix.

        If a = make_comparable(s1, l1), b = make_comparable(s2, l2), and
        a < b, then the string (s1, l1) is lexicographically less than
        the string (s2, l2). Similarly, if a > b, then (s1, l1) is
        lexicographically greater than (s2, l2). If a == b, then the
        prefixes of (s1, l1) and (s2, l2) are lexicographically equal. */
    static T make_comparable(const char *s, int len) {
        return net_to_host_order(make(s, len));
    }

    /** @brief Return a T containing data from a string's prefix.
        @pre It is safe to access characters in the range
          [@a s - size - 1, @a s + size).

        This function acts like make(), but can use single memory accesses for
        short strings. These accesses may observe data outside the range [@a
        s, @a s + len). */
    static T make_sloppy(const char *s, int len) {
        if (len <= 0)
            return 0;
#if HAVE_UNALIGNED_ACCESS
        if (len >= size)
            return *reinterpret_cast<const T *>(s);
# if WORDS_BIGENDIAN
        return *reinterpret_cast<const T *>(s) & (~T(0) << (8 * (size - len)));
# elif WORDS_BIGENDIAN_SET
        return *reinterpret_cast<const T *>(s - (size - len)) >> (8 * (size - len));
# else
#  error "WORDS_BIGENDIAN has not been set!"
# endif
#else
        union_type u(0);
        memcpy(u.s, s, std::min(len, size));
        return u.x;
#endif
    }

    /** @brief Return a T that compares similarly to a string's prefix.
        @pre It is safe to access characters in the range
          [@a s - size - 1, @a s + size).

        This function acts like make_comparable(), but can use single memory
        accesses for short strings. These accesses may observe data outside
        the range [@a s, @a s + len). */
    static T make_comparable_sloppy(const char *s, int len) {
        return net_to_host_order(make_sloppy(s, len));
    }


    /** @brief Unparse a comparable @a value into a buffer.
        @return Number of characters unparsed (<= buflen).

        If @a value was created by string_slice::make_comparable(s, x), then
        after this function returns, @a buf contains a string equal to the
        original @a s, except that trailing null characters have been
        removed. */
    static int unparse_comparable(char *buf, int buflen, T value) {
        union_type u(host_to_net_order(value));
        int l = size;
        while (l > 0 && u.s[l - 1] == 0)
            --l;
        l = std::min(l, buflen);
        memcpy(buf, u.s, l);
        return l;
    }

    /** @brief Unparse a comparable @a value into a buffer.
        @return Number of characters unparsed (<= buflen).

        If @a value was created by string_slice::make_comparable(s, @a len),
        then after this function returns, @a buf contains a string equal to
        the first @a len bytes of s. */
    static int unparse_comparable(char *buf, int buflen, T value, int len) {
        union_type u(host_to_net_order(value));
        int l = std::min(std::min(len, size), buflen);
        memcpy(buf, u.s, l);
        return l;
    }


    /** @brief Test two strings for equality.
        @param a first string
        @param b second string
        @param len number of characters in @a a and @a b
        @return true iff the two strings' first @a len characters are equal
        @pre It is safe to access characters in the ranges
          [@a a - size + 1, @a a + size) and [@a b - size + 1, @a b + size).

        Always returns the same result as "memcmp(@a a, @a b, @a len) == 0",
        but can be faster on some machines. */
    static bool equals_sloppy(const char *a, const char *b, int len) {
#if HAVE_UNALIGNED_ACCESS
        if (len <= size) {
            typename mass::make_unsigned<T>::type delta
                = *reinterpret_cast<const T *>(a)
                ^ *reinterpret_cast<const T *>(b);
            if (unlikely(len <= 0))
                return true;
# if WORDS_BIGENDIAN
            return (delta >> (8 * (size - len))) == 0;
# else
            return (delta << (8 * (size - len))) == 0;
# endif
        }
#endif
        return memcmp(a, b, len) == 0;
    }
};

template <typename T> constexpr int string_slice<T>::size;

#endif

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
#ifndef KPERMUTER_HH
#define KPERMUTER_HH
#include "string.hh"

class identity_kpermuter {
    int size_;
  public:
    identity_kpermuter(int size)
        : size_(size) {
    }

    int size() const {
        return size_;
    }
    int operator[](int i) const {
        return i;
    }
    bool operator==(const identity_kpermuter&) const {
        return true;
    }
    bool operator!=(const identity_kpermuter&) const {
        return false;
    }
};

template <int C> struct sized_kpermuter_info {};
template <> struct sized_kpermuter_info<0> {
    typedef uint16_t storage_type;
    typedef unsigned value_type;
    enum { initial_value = 0x0120U, full_value = 0x2100U };
};
template <> struct sized_kpermuter_info<1> {
    typedef uint32_t storage_type;
    typedef unsigned value_type;
    enum { initial_value = 0x01234560U, full_value = 0x65432100U };
};
template <> struct sized_kpermuter_info<2> {
    typedef uint64_t storage_type;
    typedef uint64_t value_type;
    enum { initial_value = (uint64_t) 0x0123456789ABCDE0ULL,
           full_value = (uint64_t) 0xEDCBA98765432100ULL };
};

template <int width> class kpermuter {
  public:
    typedef sized_kpermuter_info<(width > 3) + (width > 7) + (width > 15)> info;
    typedef typename info::storage_type storage_type;
    typedef typename info::value_type value_type;
    enum { max_width = (int) (sizeof(storage_type) * 2 - 1) };
    enum { size_bits = 4 };

    /** @brief Construct an uninitialized permuter. */
    kpermuter() {
    }
    /** @brief Construct a permuter with value @a x. */
    kpermuter(value_type x)
        : x_(x) {
    }

    /** @brief Return an empty permuter with size 0.

        Elements will be allocated in order 0, 1, ..., @a width - 1. */
    static inline value_type make_empty() {
        value_type p = (value_type) info::initial_value >> ((max_width - width) << 2);
        return p & ~(value_type) 15;
    }
    /** @brief Return a permuter with size @a n.

        The returned permutation has size() @a n. For 0 <= i < @a n,
        (*this)[i] == i. Elements n through @a width - 1 are free, and will be
        allocated in that order. */
    static inline value_type make_sorted(int n) {
        value_type mask = (n == width ? (value_type) 0 : (value_type) 16 << (n << 2)) - 1;
        return (make_empty() << (n << 2))
            | ((value_type) info::full_value & mask)
            | n;
    }

    /** @brief Return the permuter's size. */
    int size() const {
        return x_ & 15;
    }
    /** @brief Return the permuter's element @a i.
        @pre 0 <= i < width */
    int operator[](int i) const {
        return (x_ >> ((i << 2) + 4)) & 15;
    }
    int back() const {
        return (*this)[width - 1];
    }
    value_type value() const {
        return x_;
    }
    value_type value_from(int i) const {
        return x_ >> ((i + 1) << 2);
    }

    void set_size(int n) {
        x_ = (x_ & ~(value_type) 15) | n;
    }
    /** @brief Allocate a new element and insert it at position @a i.
        @pre 0 <= @a i < @a width
        @pre size() < @a width
        @return The newly allocated element.

        Consider the following code:
        <code>
        kpermuter<...> p = ..., q = p;
        int x = q.insert_from_back(i);
        </code>

        The modified permuter, q, has the following properties.
        <ul>
        <li>q.size() == p.size() + 1</li>
        <li>Given j with 0 <= j < i, q[j] == p[j] && q[j] != x</li>
        <li>Given j with j == i, q[j] == x</li>
        <li>Given j with i < j < q.size(), q[j] == p[j-1] && q[j] != x</li>
        </ul> */
    int insert_from_back(int i) {
        int value = back();
        // increment size, leave lower slots unchanged
        x_ = ((x_ + 1) & (((value_type) 16 << (i << 2)) - 1))
            // insert slot
            | ((value_type) value << ((i << 2) + 4))
            // shift up unchanged higher entries & empty slots
            | ((x_ << 4) & ~(((value_type) 256 << (i << 2)) - 1));
        return value;
    }
    /** @brief Insert an unallocated element from position @a si at position @a di.
        @pre 0 <= @a di < @a width
        @pre size() < @a width
        @pre size() <= @a si
        @return The newly allocated element. */
    void insert_selected(int di, int si) {
        (void) width;
        int value = (*this)[si];
        value_type mask = ((value_type) 256 << (si << 2)) - 1;
        // increment size, leave lower slots unchanged
        x_ = ((x_ + 1) & (((value_type) 16 << (di << 2)) - 1))
            // insert slot
            | ((value_type) value << ((di << 2) + 4))
            // shift up unchanged higher entries & empty slots
            | ((x_ << 4) & mask & ~(((value_type) 256 << (di << 2)) - 1))
            // leave uppermost slots alone
            | (x_ & ~mask);
    }
    /** @brief Remove the element at position @a i.
        @pre 0 <= @a i < @a size()
        @pre size() < @a width

        Consider the following code:
        <code>
        kpermuter<...> p = ..., q = p;
        q.remove(i);
        </code>

        The modified permuter, q, has the following properties.
        <ul>
        <li>q.size() == p.size() - 1</li>
        <li>Given j with 0 <= j < i, q[j] == p[j]</li>
        <li>Given j with i <= j < q.size(), q[j] == p[j+1]</li>
        <li>q[q.size()] == p[i]</li>
        </ul> */
    void remove(int i) {
        (void) width;
        if (int(x_ & 15) == i + 1)
            --x_;
        else {
            int rot_amount = ((x_ & 15) - i - 1) << 2;
            value_type rot_mask =
                (((value_type) 16 << rot_amount) - 1) << ((i + 1) << 2);
            // decrement size, leave lower slots unchanged
            x_ = ((x_ - 1) & ~rot_mask)
                // shift higher entries down
                | (((x_ & rot_mask) >> 4) & rot_mask)
                // shift value up
                | (((x_ & rot_mask) << rot_amount) & rot_mask);
        }
    }
    /** @brief Remove the element at position @a i to the back.
        @pre 0 <= @a i < @a size()
        @pre size() < @a width

        Consider the following code:
        <code>
        kpermuter<...> p = ..., q = p;
        q.remove_to_back(i);
        </code>

        The modified permuter, q, has the following properties.
        <ul>
        <li>q.size() == p.size() - 1</li>
        <li>Given j with 0 <= j < i, q[j] == p[j]</li>
        <li>Given j with i <= j < @a width - 1, q[j] == p[j+1]</li>
        <li>q.back() == p[i]</li>
        </ul> */
    void remove_to_back(int i) {
        value_type mask = ~(((value_type) 16 << (i << 2)) - 1);
        // clear unused slots
        value_type x = x_ & (((value_type) 16 << (width << 2)) - 1);
        // decrement size, leave lower slots unchanged
        x_ = ((x - 1) & ~mask)
            // shift higher entries down
            | ((x >> 4) & mask)
            // shift removed element up
            | ((x & mask) << ((width - i - 1) << 2));
    }
    /** @brief Rotate the permuter's elements between @a i and size().
        @pre 0 <= @a i <= @a j <= size()

        Consider the following code:
        <code>
        kpermuter<...> p = ..., q = p;
        q.rotate(i, j);
        </code>

        The modified permuter, q, has the following properties.
        <ul>
        <li>q.size() == p.size()</li>
        <li>Given k with 0 <= k < i, q[k] == p[k]</li>
        <li>Given k with i <= k < q.size(), q[k] == p[i + (k - i + j - i) mod (size() - i)]</li>
        </ul> */
    void rotate(int i, int j) {
        value_type mask = (i == width ? (value_type) 0 : (value_type) 16 << (i << 2)) - 1;
        // clear unused slots
        value_type x = x_ & (((value_type) 16 << (width << 2)) - 1);
        x_ = (x & mask)
            | ((x >> ((j - i) << 2)) & ~mask)
            | ((x & ~mask) << ((width - j) << 2));
    }
    /** @brief Exchange the elements at positions @a i and @a j. */
    void exchange(int i, int j) {
        value_type diff = ((x_ >> (i << 2)) ^ (x_ >> (j << 2))) & 240;
        x_ ^= (diff << (i << 2)) | (diff << (j << 2));
    }
    /** @brief Exchange positions of values @a x and @a y. */
    void exchange_values(int x, int y) {
        value_type diff = 0, p = x_;
        for (int i = 0; i < width; ++i, diff <<= 4, p <<= 4) {
            int v = (p >> (width << 2)) & 15;
            diff ^= -((v == x) | (v == y)) & (x ^ y);
        }
        x_ ^= diff;
    }

    lcdf::String unparse() const;

    bool operator==(const kpermuter<width>& x) const {
        return x_ == x.x_;
    }
    bool operator!=(const kpermuter<width>& x) const {
        return !(*this == x);
    }

    static inline int size(value_type p) {
        return p & 15;
    }
  private:
    value_type x_;
};

template <int width>
lcdf::String kpermuter<width>::unparse() const
{
    char buf[max_width + 3], *s = buf;
    value_type p(x_);
    value_type seen(0);
    int n = p & 15;
    p >>= 4;
    for (int i = 0; true; ++i) {
        if (i == n)
            *s++ = ':';
        if (i == width)
            break;
        if ((p & 15) < 10)
            *s++ = '0' + (p & 15);
        else
            *s++ = 'a' + (p & 15) - 10;
        seen |= 1 << (p & 15);
        p >>= 4;
    }
    if (seen != (1 << width) - 1) {
        *s++ = '?';
        *s++ = '!';
    }
    return lcdf::String(buf, s);
}


template <typename T> struct has_permuter_type {
    template <typename C> static char test(typename C::permuter_type *);
    template <typename> static int test(...);
    static constexpr bool value = sizeof(test<T>(0)) == 1;
};

template <typename T, bool HP = has_permuter_type<T>::value> struct key_permuter {};
template <typename T> struct key_permuter<T, true> {
    typedef typename T::permuter_type type;
    static type permutation(const T& n) {
        return n.permutation();
    }
};
template <typename T> struct key_permuter<T, false> {
    typedef identity_kpermuter type;
    static type permutation(const T& n) {
        return identity_kpermuter(n.size());
    }
};

#endif

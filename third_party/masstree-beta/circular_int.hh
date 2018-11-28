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
#ifndef KVDB_CIRCULAR_INT_HH
#define KVDB_CIRCULAR_INT_HH 1
#include "compiler.hh"

template <typename T>
class circular_int {
  public:
    typedef typename mass::make_unsigned<T>::type value_type;
    typedef typename mass::make_signed<T>::type difference_type;

    circular_int()
        : v_() {
    }
    circular_int(T x)
        : v_(x) {
    }

    value_type value() const {
        return v_;
    }

    circular_int<T> &operator++() {
        ++v_;
        return *this;
    }
    circular_int<T> operator++(int) {
        ++v_;
        return circular_int<T>(v_ - 1);
    }
    circular_int<T> &operator--() {
        --v_;
        return *this;
    }
    circular_int<T> operator--(int) {
        --v_;
        return circular_int<T>(v_ + 1);
    }
    circular_int<T> &operator+=(unsigned x) {
        v_ += x;
        return *this;
    }
    circular_int<T> &operator+=(int x) {
        v_ += x;
        return *this;
    }
    circular_int<T> &operator-=(unsigned x) {
        v_ -= x;
        return *this;
    }
    circular_int<T> &operator-=(int x) {
        v_ -= x;
        return *this;
    }

    circular_int<T> cmpxchg(circular_int<T> expected, circular_int<T> desired) {
        return ::cmpxchg(&v_, expected.v_, desired.v_);
    }
    circular_int<T> cmpxchg(T expected, T desired) {
        return ::cmpxchg(&v_, expected, desired);
    }

    typedef value_type (circular_int<T>::*unspecified_bool_type)() const;
    operator unspecified_bool_type() const {
        return v_ != 0 ? &circular_int<T>::value : 0;
    }
    bool operator!() const {
        return v_ == 0;
    }

    circular_int<T> operator+(unsigned x) const {
        return circular_int<T>(v_ + x);
    }
    circular_int<T> operator+(int x) const {
        return circular_int<T>(v_ + x);
    }
    circular_int<T> next_nonzero() const {
        value_type v = v_ + 1;
        return circular_int<T>(v + !v);
    }
    static value_type next_nonzero(value_type x) {
        ++x;
        return x + !x;
    }
    circular_int<T> operator-(unsigned x) const {
        return circular_int<T>(v_ - x);
    }
    circular_int<T> operator-(int x) const {
        return circular_int<T>(v_ - x);
    }
    difference_type operator-(circular_int<T> x) const {
        return v_ - x.v_;
    }

    bool operator==(circular_int<T> x) const {
        return v_ == x.v_;
    }
    bool operator!=(circular_int<T> x) const {
        return !(*this == x);
    }
    static bool less(value_type a, value_type b) {
        return difference_type(a - b) < 0;
    }
    static bool less_equal(value_type a, value_type b) {
        return difference_type(a - b) <= 0;
    }
    bool operator<(circular_int<T> x) const {
        return less(v_, x.v_);
    }
    bool operator<=(circular_int<T> x) const {
        return !less(x.v_, v_);
    }
    bool operator>=(circular_int<T> x) const {
        return !less(v_, x.v_);
    }
    bool operator>(circular_int<T> x) const {
        return less(x.v_, v_);
    }

  private:
    value_type v_;
};

typedef circular_int<uint64_t> kvepoch_t;

template <typename T>
inline circular_int<T> cmpxchg(circular_int<T> *object, circular_int<T> expected,
                               circular_int<T> desired) {
    return object->cmpxchg(expected, desired);
}

#endif

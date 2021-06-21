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
#ifndef KVRANDOM_HH
#define KVRANDOM_HH 1
#include <inttypes.h>
#include <stdlib.h>
#include <random>

// A simple LCG with parameters from Numerical Recipes.
class kvrandom_lcg_nr_simple {
public:
    using result_type = uint32_t;
    using seed_type = uint32_t;
    static constexpr result_type min() {
        return 0;
    }
    static constexpr result_type max() {
        return 0xFFFFFFFFU;
    }

    kvrandom_lcg_nr_simple()
        : seed_(default_seed) {
    }
    explicit kvrandom_lcg_nr_simple(seed_type s)
        : seed_(s) {
    }
    void seed(seed_type s) {
        seed_ = s;
    }
    result_type operator()() {
        seed_ = seed_ * a + c;
        return (seed_ = seed_ * a + c);
    }

private:
    uint32_t seed_;
    enum { default_seed = 819234718U, a = 1664525U, c = 1013904223U };
};

// A combination version of the NR LCG that uses only its higher order
// digits. (In the default NR LCG the lowest bits have less randomness; e.g.,
// the low bit flips between 0 and 1 with every call.)
class kvrandom_lcg_nr : public kvrandom_lcg_nr_simple {
public:
    static constexpr result_type max() {
        return 0x7FFFFFFFU;
    }

    result_type operator()() {
        uint32_t x0 = kvrandom_lcg_nr_simple::operator()();
        uint32_t x1 = kvrandom_lcg_nr_simple::operator()();
        return (x0 >> 15) | ((x1 & 0x7FFE) << 16);
    }
};

// A random number generator taken from NR's ran4. Based on hashing.
class kvrandom_psdes_nr {
public:
    using result_type = uint32_t;
    using seed_type = uint32_t;
    static constexpr result_type min() {
        return 0;
    }
    static constexpr result_type max() {
        return 0xFFFFFFFFU;
    }

    kvrandom_psdes_nr() {
        seed(1);
    }
    explicit kvrandom_psdes_nr(seed_type s) {
        seed(s);
    }
    void seed(seed_type s) {
        seed_ = s;
        next_ = 1;
    }
    result_type operator()() {
        uint32_t value = psdes(seed_, next_);
        ++next_;
        return value;
    }
    result_type operator[](uint32_t index) const {
        return psdes(seed_, index);
    }
private:
    uint32_t seed_;
    uint32_t next_;
    enum { niter = 4 };
    static const uint32_t c1[niter], c2[niter];
    static uint32_t psdes(uint32_t lword, uint32_t irword);
};

// a wrapper around random(), for backwards compatibility
class kvrandom_random {
public:
    using result_type = uint32_t;
    static constexpr result_type min() {
        return 0;
    }
    static constexpr result_type max() {
        return 0x7FFFFFFFU;
    }

    kvrandom_random() {
    }
    void seed(uint32_t s) {
        srandom(s);
    }
    result_type operator()() {
        return random();
    }
};

// a modulus-based, i.e. incorrect, version of uniform_int_distribution
// that is faster than the standard
template <typename T = int>
class kvrandom_uniform_int_distribution {
public:
    using result_type = T;

    kvrandom_uniform_int_distribution(T a, T b)
        : a_(a), n_(b - a + 1) {
    }
    template <typename G>
    result_type operator()(G& g) const {
        return a_ + g() % n_;
    }
private:
    result_type a_;
    result_type n_;
};

// the std::bernoulli_distribution is fast enough
using kvrandom_bernoulli_distribution = std::bernoulli_distribution;

#endif

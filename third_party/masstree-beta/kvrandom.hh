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

// A simple LCG with parameters from Numerical Recipes.
class kvrandom_lcg_nr_simple { public:
    enum { min_value = 0, max_value = 0xFFFFFFFFU };
    typedef uint32_t value_type;
    typedef uint32_t seed_type;
    kvrandom_lcg_nr_simple()
	: seed_(default_seed) {
    }
    explicit kvrandom_lcg_nr_simple(seed_type seed)
	: seed_(seed) {
    }
    void reset(seed_type seed) {
	seed_ = seed;
    }
    value_type next() {
	return (seed_ = seed_ * a + c);
    }
  private:
    uint32_t seed_;
    enum { default_seed = 819234718U, a = 1664525U, c = 1013904223U };
};

// A combination version of the NR LCG that uses only its higher order
// digits. (In the default NR LCG the lowest bits have less randomness; e.g.,
// the low bit flips between 0 and 1 with every call.)
class kvrandom_lcg_nr : public kvrandom_lcg_nr_simple { public:
    enum { min_value = 0, max_value = 0x7FFFFFFF };
    typedef int32_t value_type;
    value_type next() {
	uint32_t x0 = kvrandom_lcg_nr_simple::next(),
	    x1 = kvrandom_lcg_nr_simple::next();
	return (x0 >> 15) | ((x1 & 0x7FFE) << 16);
    }
};

// A random number generator taken from NR's ran4. Based on hashing.
class kvrandom_psdes_nr { public:
    enum { min_value = 0, max_value = 0xFFFFFFFFU };
    typedef uint32_t value_type;
    typedef uint32_t seed_type;
    kvrandom_psdes_nr() {
	reset(1);
    }
    explicit kvrandom_psdes_nr(seed_type seed) {
	reset(seed);
    }
    void reset(seed_type seed) {
	seed_ = seed;
	next_ = 1;
    }
    value_type next() {
	uint32_t value = psdes(seed_, next_);
	++next_;
	return value;
    }
    value_type operator[](uint32_t index) const {
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
class kvrandom_random { public:
    kvrandom_random() {
    }
    void reset(uint32_t seed) {
	srandom(seed);
    }
    int32_t next() const {
	return random();
    }
};

#endif

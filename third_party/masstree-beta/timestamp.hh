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
#ifndef TIMESTAMP_HH
#define TIMESTAMP_HH
#include "compiler.hh"
#include <time.h>
#include <sys/time.h>
#include <math.h>

#if HAVE_INT64_T_IS_LONG_LONG
#define PRIuKVTS "llu"
#else
#define PRIuKVTS "lu"
#endif
#define PRIKVTSPARTS "%lu.%06lu"

#define KVTS_HIGHPART(t) ((unsigned long) ((t) >> 32))
#define KVTS_LOWPART(t) ((unsigned long) (uint32_t) (t))

typedef uint64_t kvtimestamp_t;

inline kvtimestamp_t timestamp() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return ((kvtimestamp_t) tv.tv_sec << 32) | (unsigned int)tv.tv_usec;
}

inline kvtimestamp_t timestamp_sub(kvtimestamp_t a, kvtimestamp_t b) {
    a -= b;
    if (KVTS_LOWPART(a) > 999999)
	a -= ((kvtimestamp_t) 1 << 32) - 1000000;
    return a;
}

extern kvtimestamp_t initial_timestamp;

inline double now() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

inline struct timespec &set_timespec(struct timespec &x, double y) {
    double ipart = floor(y);
    x.tv_sec = (long) ipart;
    x.tv_nsec = (long) ((y - ipart) * 1e9);
    return x;
}

#endif

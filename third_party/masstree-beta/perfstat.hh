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
#ifndef PERF_STAT_HH
#define PERF_STAT_HH 1
#include "compiler.hh"
#include "misc.hh"
#include <stdlib.h>
#include <inttypes.h>

namespace Perf {
struct stat {
    /** @brief An initialization call from main function
     */
    static void initmain(bool pinthreads);
#if GCSTATS
    int gc_nfree;
#endif
    void initialize(int cid) {
        this->cid = cid;
    }
    static void print(const stat **s, int n);
    int cid;    // core index
};
}
#endif

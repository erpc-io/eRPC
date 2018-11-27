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
#ifndef MASSTREE_CHECKPOINT_HH
#define MASSTREE_CHECKPOINT_HH
#include "kvrow.hh"
#include "kvio.hh"
#include "msgpack.hh"

struct ckstate {
    kvout *vals; // key, val, timestamp in msgpack
    uint64_t count; // total nodes written
    uint64_t bytes;
    pthread_cond_t state_cond;
    volatile int state;
    threadinfo *ti;
    Str startkey;
    Str endkey;

    template <typename SS, typename K>
    void visit_leaf(const SS&, const K&, threadinfo&) {
    }
    bool visit_value(Str key, const row_type* value, threadinfo& ti);

    template <typename T>
    static void insert(T& table, msgpack::parser& par, threadinfo& ti);
};

template <typename T>
void ckstate::insert(T& table, msgpack::parser& par, threadinfo& ti) {
    Str key;
    kvtimestamp_t ts{};
    par >> key >> ts;
    row_type* row = row_type::checkpoint_read(par, ts, ti);

    typename T::cursor_type lp(table, key);
    bool found = lp.find_insert(ti);
    masstree_invariant(!found); (void) found;
    ti.observe_phantoms(lp.node());
    lp.value() = row;
    lp.finish(1, ti);
}

#endif

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
#ifndef MTCOUNTERS_HH
#define MTCOUNTERS_HH 1

enum memtag {
    // memtags are divided into a *type* and a *pool*.
    // The type is purely for debugging. The pool indicates the pool from
    // which an allocation was taken.
    memtag_none = 0x000,
    memtag_value = 0x100,
    memtag_limbo = 0x500,
    memtag_masstree_leaf = 0x1000,
    memtag_masstree_internode = 0x1100,
    memtag_masstree_ksuffixes = 0x1200,
    memtag_masstree_gc = 0x1300,
    memtag_pool_mask = 0xFF
};

enum threadcounter {
    // order is important among tc_alloc constants:
    tc_alloc,
    tc_alloc_value = tc_alloc,
    tc_alloc_other = tc_alloc + 1,
    // end tc_alloc constants
    tc_gc,
    tc_limbo_slots,
    tc_replay_create_delta,
    tc_replay_remove_delta,
    tc_root_retry,
    tc_internode_retry,
    tc_leaf_retry,
    tc_leaf_walk,
    // order is important among tc_stable constants:
    tc_stable,
    tc_stable_internode_insert = tc_stable + 0,
    tc_stable_internode_split = tc_stable + 1,
    tc_stable_leaf_insert = tc_stable + 2,
    tc_stable_leaf_split = tc_stable + 3,
    // end tc_stable constants
    tc_internode_lock,
    tc_leaf_lock,
    tc_max
};

#endif

/* Masstree
 * Eddie Kohler, Yandong Mao, Robert Morris
 * Copyright (c) 2012-2016 President and Fellows of Harvard College
 * Copyright (c) 2012-2016 Massachusetts Institute of Technology
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
#include "memdebug.hh"
#include <stdio.h>
#include <assert.h>

#if HAVE_MEMDEBUG
void memdebug::landmark(char* buf, size_t bufsz) const {
    if (this->magic != magic_value && this->magic != magic_free_value)
        snprintf(buf, bufsz, "???");
    else if (this->file)
        snprintf(buf, bufsz, "%s:%d", this->file, this->line);
    else if (this->line)
        snprintf(buf, bufsz, "%d", this->line);
    else
        snprintf(buf, bufsz, "0");
}

void
memdebug::hard_free_checks(const memdebug *m, size_t sz, memtag tag,
                           int after_rcu, const char *op) {
    char buf[256];
    m->landmark(buf, sizeof(buf));
    if (m->magic == magic_free_value)
        fprintf(stderr, "%s(%p): double free, was @%s\n",
                op, m + 1, buf);
    else if (m->magic != magic_value)
        fprintf(stderr, "%s(%p): freeing unallocated pointer (%x)\n",
                op, m + 1, m->magic);
    assert(m->magic == magic_value);
    if (tag && m->tag != tag)
        fprintf(stderr, "%s(%p): expected type %x, saw %x, "
                "allocated %s\n", op, m + 1, tag, m->tag, buf);
    if (!after_rcu && m->size != sz)
        fprintf(stderr, "%s(%p): expected size %lu, saw %lu, "
                "allocated %s\n", op, m + 1,
                (unsigned long) sz, (unsigned long) m->size, buf);
    if (m->after_rcu != after_rcu)
        fprintf(stderr, "%s(%p): double free after rcu, allocated @%s\n",
                op, m + 1, buf);
    if (tag)
        assert(m->tag == tag);
    if (!after_rcu)
        assert(m->size == sz);
    assert(m->after_rcu == after_rcu);
}

void
memdebug::hard_assert_use(const void* ptr, memtag allowed) {
    const memdebug* m = reinterpret_cast<const memdebug*>(ptr) - 1;
    char buf[256];
    m->landmark(buf, sizeof(buf));
    if (m->magic == magic_free_value)
        fprintf(stderr, "%p: use tag %x after free, allocated %s\n",
                m + 1, allowed, buf);
    else if (m->magic != magic_value)
        fprintf(stderr, "%p: pointer is unallocated, not tag %x\n",
                m + 1, allowed);
    assert(m->magic == magic_value);
    if (allowed != 0 && (m->tag ^ allowed) > memtag_pool_mask)
        fprintf(stderr, "%p: expected tag %x, got tag %x, allocated %s\n",
                m + 1, allowed, m->tag, buf);
    if (allowed != 0)
        assert((m->tag ^ allowed) <= memtag_pool_mask);
}
#endif

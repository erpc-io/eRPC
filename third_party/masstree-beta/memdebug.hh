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
#ifndef MEMDEBUG_HH
#define MEMDEBUG_HH 1
#include "mtcounters.hh"
#include <stddef.h>

struct memdebug {
    static inline void* make(void* ptr, size_t sz, memtag tag);
    static inline void set_landmark(void* ptr, const char* file, int line);
    static inline void* check_free(void* ptr, size_t sz, memtag tag);
    static inline void check_rcu(void* ptr, size_t sz, memtag tag);
    static inline void* check_free_after_rcu(void* ptr, memtag tag);
    static inline bool check_use(const void* ptr, memtag allowed);
    static inline void assert_use(const void* ptr, memtag allowed);

#if HAVE_MEMDEBUG
private:
    enum {
        magic_value = 389612313 /* = 0x17390319 */,
        magic_free_value = 2015593488 /* = 0x78238410 */
    };
    int magic;
    memtag tag;
    size_t size;
    int after_rcu;
    int line;
    const char* file;

    static void free_checks(const memdebug* m, size_t size, memtag tag,
                            int after_rcu, const char* op) {
        if (m->magic != magic_value
            || m->tag != tag
            || (!after_rcu && m->size != size)
            || m->after_rcu != after_rcu)
            hard_free_checks(m, size, tag, after_rcu, op);
    }
    void landmark(char* buf, size_t size) const;
    static void hard_free_checks(const memdebug* m, size_t size, memtag tag,
                                 int after_rcu, const char* op);
    static void hard_assert_use(const void* ptr, memtag allowed);
#endif
};

enum {
#if HAVE_MEMDEBUG
    memdebug_size = sizeof(memdebug)
#else
    memdebug_size = 0
#endif
};

inline void* memdebug::make(void* ptr, size_t sz, memtag tag) {
#if HAVE_MEMDEBUG
    if (ptr) {
        memdebug* m = reinterpret_cast<memdebug*>(ptr);
        m->magic = magic_value;
        m->tag = tag;
        m->size = sz;
        m->after_rcu = 0;
        m->line = 0;
        m->file = 0;
        return m + 1;
    } else
        return ptr;
#else
    (void) sz, (void) tag;
    return ptr;
#endif
}

inline void memdebug::set_landmark(void* ptr, const char* file, int line) {
#if HAVE_MEMDEBUG
    if (ptr) {
        memdebug* m = reinterpret_cast<memdebug*>(ptr) - 1;
        m->file = file;
        m->line = line;
    }
#else
    (void) ptr, (void) file, (void) line;
#endif
}

inline void* memdebug::check_free(void* ptr, size_t sz, memtag tag) {
#if HAVE_MEMDEBUG
    memdebug* m = reinterpret_cast<memdebug*>(ptr) - 1;
    free_checks(m, sz, tag, false, "deallocate");
    m->magic = magic_free_value;
    return m;
#else
    (void) sz, (void) tag;
    return ptr;
#endif
}

inline void memdebug::check_rcu(void* ptr, size_t sz, memtag tag) {
#if HAVE_MEMDEBUG
    memdebug* m = reinterpret_cast<memdebug*>(ptr) - 1;
    free_checks(m, sz, tag, false, "deallocate_rcu");
    m->after_rcu = 1;
#else
    (void) ptr, (void) sz, (void) tag;
#endif
}

inline void* memdebug::check_free_after_rcu(void* ptr, memtag tag) {
#if HAVE_MEMDEBUG
    memdebug* m = reinterpret_cast<memdebug*>(ptr) - 1;
    free_checks(m, 0, tag, true, "free_after_rcu");
    m->magic = magic_free_value;
    return m;
#else
    (void) tag;
    return ptr;
#endif
}

inline bool memdebug::check_use(const void* ptr, memtag allowed) {
#if HAVE_MEMDEBUG
    const memdebug* m = reinterpret_cast<const memdebug*>(ptr) - 1;
    return m->magic == magic_value && (allowed == 0 || (m->tag ^ allowed) <= memtag_pool_mask);
#else
    (void) ptr, (void) allowed;
    return true;
#endif
}

inline void memdebug::assert_use(const void* ptr, memtag allowed) {
#if HAVE_MEMDEBUG
    if (!check_use(ptr, allowed))
        hard_assert_use(ptr, allowed);
#else
    (void) ptr, (void) allowed;
#endif
}

#endif

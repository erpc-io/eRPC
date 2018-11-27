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
#ifndef KVTHREAD_HH
#define KVTHREAD_HH 1
#include "mtcounters.hh"
#include "compiler.hh"
#include "circular_int.hh"
#include "timestamp.hh"
#include "memdebug.hh"
#include <assert.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stdlib.h>

class threadinfo;
class loginfo;

typedef uint64_t mrcu_epoch_type;
typedef int64_t mrcu_signed_epoch_type;

extern volatile mrcu_epoch_type globalepoch;  // global epoch, updated regularly
extern volatile mrcu_epoch_type active_epoch;

struct limbo_group {
    typedef mrcu_epoch_type epoch_type;
    typedef mrcu_signed_epoch_type signed_epoch_type;

    struct limbo_element {
        void* ptr_;
        union {
            memtag tag;
            epoch_type epoch;
        } u_;
    };

    enum { capacity = (4076 - sizeof(epoch_type) - sizeof(limbo_group*)) / sizeof(limbo_element) };
    unsigned head_;
    unsigned tail_;
    epoch_type epoch_;
    limbo_group* next_;
    limbo_element e_[capacity];
    limbo_group()
        : head_(0), tail_(0), next_() {
    }
    epoch_type first_epoch() const {
        assert(head_ != tail_);
        return e_[head_].u_.epoch;
    }
    void push_back(void* ptr, memtag tag, mrcu_epoch_type epoch) {
        assert(tail_ + 2 <= capacity);
        if (head_ == tail_ || epoch_ != epoch) {
            e_[tail_].ptr_ = nullptr;
            e_[tail_].u_.epoch = epoch;
            epoch_ = epoch;
            ++tail_;
        }
        e_[tail_].ptr_ = ptr;
        e_[tail_].u_.tag = tag;
        ++tail_;
    }
    inline unsigned clean_until(threadinfo& ti, mrcu_epoch_type epoch_bound, unsigned count);
};

template <int N> struct has_threadcounter {
    static bool test(threadcounter ci) {
        return unsigned(ci) < unsigned(N);
    }
};
template <> struct has_threadcounter<0> {
    static bool test(threadcounter) {
        return false;
    }
};

struct mrcu_callback {
    virtual ~mrcu_callback() {
    }
    virtual void operator()(threadinfo& ti) = 0;
};

class threadinfo {
  public:
    enum {
        TI_MAIN, TI_PROCESS, TI_LOG, TI_CHECKPOINT
    };

    static threadinfo* allthreads;

    threadinfo* next() const {
        return next_;
    }

    static threadinfo* make(int purpose, int index);
    // XXX destructor

    // thread information
    int purpose() const {
        return purpose_;
    }
    int index() const {
        return index_;
    }
    loginfo* logger() const {
        return logger_;
    }
    void set_logger(loginfo* logger) {
        assert(!logger_ && logger);
        logger_ = logger;
    }

    // timestamps
    kvtimestamp_t operation_timestamp() const {
        return timestamp();
    }
    kvtimestamp_t update_timestamp() const {
        return ts_;
    }
    kvtimestamp_t update_timestamp(kvtimestamp_t x) const {
        if (circular_int<kvtimestamp_t>::less_equal(ts_, x))
            // x might be a marker timestamp; ensure result is not
            ts_ = (x | 1) + 1;
        return ts_;
    }
    template <typename N> void observe_phantoms(N* n) {
        if (circular_int<kvtimestamp_t>::less(ts_, n->phantom_epoch_[0]))
            ts_ = n->phantom_epoch_[0];
    }

    // event counters
    void mark(threadcounter ci) {
        if (has_threadcounter<int(ncounters)>::test(ci))
            ++counters_[ci];
    }
    void mark(threadcounter ci, int64_t delta) {
        if (has_threadcounter<int(ncounters)>::test(ci))
            counters_[ci] += delta;
    }
    void set_counter(threadcounter ci, uint64_t value) {
        if (has_threadcounter<int(ncounters)>::test(ci))
            counters_[ci] = value;
    }
    bool has_counter(threadcounter ci) const {
        return has_threadcounter<int(ncounters)>::test(ci);
    }
    uint64_t counter(threadcounter ci) const {
        return has_threadcounter<int(ncounters)>::test(ci) ? counters_[ci] : 0;
    }

    struct accounting_relax_fence_function {
        threadinfo* ti_;
        threadcounter ci_;
        accounting_relax_fence_function(threadinfo* ti, threadcounter ci)
            : ti_(ti), ci_(ci) {
        }
        void operator()() {
            relax_fence();
            ti_->mark(ci_);
        }
    };
    /** @brief Return a function object that calls mark(ci); relax_fence().
     *
     * This function object can be used to count the number of relax_fence()s
     * executed. */
    accounting_relax_fence_function accounting_relax_fence(threadcounter ci) {
        return accounting_relax_fence_function(this, ci);
    }

    struct stable_accounting_relax_fence_function {
        threadinfo* ti_;
        stable_accounting_relax_fence_function(threadinfo* ti)
            : ti_(ti) {
        }
        template <typename V>
        void operator()(V v) {
            relax_fence();
            ti_->mark(threadcounter(tc_stable + (v.isleaf() << 1) + v.splitting()));
        }
    };
    /** @brief Return a function object that calls mark(ci); relax_fence().
     *
     * This function object can be used to count the number of relax_fence()s
     * executed. */
    stable_accounting_relax_fence_function stable_fence() {
        return stable_accounting_relax_fence_function(this);
    }

    accounting_relax_fence_function lock_fence(threadcounter ci) {
        return accounting_relax_fence_function(this, ci);
    }

    // memory allocation
    void* allocate(size_t sz, memtag tag) {
        void* p = malloc(sz + memdebug_size);
        p = memdebug::make(p, sz, tag);
        if (p)
            mark(threadcounter(tc_alloc + (tag > memtag_value)), sz);
        return p;
    }
    void deallocate(void* p, size_t sz, memtag tag) {
        // in C++ allocators, 'p' must be nonnull
        assert(p);
        p = memdebug::check_free(p, sz, tag);
        free(p);
        mark(threadcounter(tc_alloc + (tag > memtag_value)), -sz);
    }
    void deallocate_rcu(void* p, size_t sz, memtag tag) {
        assert(p);
        memdebug::check_rcu(p, sz, tag);
        record_rcu(p, tag);
        mark(threadcounter(tc_alloc + (tag > memtag_value)), -sz);
    }

    void* pool_allocate(size_t sz, memtag tag) {
        int nl = (sz + memdebug_size + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE;
        assert(nl <= pool_max_nlines);
        if (unlikely(!pool_[nl - 1]))
            refill_pool(nl);
        void* p = pool_[nl - 1];
        if (p) {
            pool_[nl - 1] = *reinterpret_cast<void **>(p);
            p = memdebug::make(p, sz, memtag(tag + nl));
            mark(threadcounter(tc_alloc + (tag > memtag_value)),
                 nl * CACHE_LINE_SIZE);
        }
        return p;
    }
    void pool_deallocate(void* p, size_t sz, memtag tag) {
        int nl = (sz + memdebug_size + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE;
        assert(p && nl <= pool_max_nlines);
        p = memdebug::check_free(p, sz, memtag(tag + nl));
        if (use_pool()) {
            *reinterpret_cast<void **>(p) = pool_[nl - 1];
            pool_[nl - 1] = p;
        } else
            free(p);
        mark(threadcounter(tc_alloc + (tag > memtag_value)),
             -nl * CACHE_LINE_SIZE);
    }
    void pool_deallocate_rcu(void* p, size_t sz, memtag tag) {
        int nl = (sz + memdebug_size + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE;
        assert(p && nl <= pool_max_nlines);
        memdebug::check_rcu(p, sz, memtag(tag + nl));
        record_rcu(p, memtag(tag + nl));
        mark(threadcounter(tc_alloc + (tag > memtag_value)),
             -nl * CACHE_LINE_SIZE);
    }

    // RCU
    enum { rcu_free_count = 128 }; // max # of entries to free per rcu_quiesce() call
    void rcu_start() {
        if (gc_epoch_ != globalepoch)
            gc_epoch_ = globalepoch;
    }
    void rcu_stop() {
        if (perform_gc_epoch_ != active_epoch)
            hard_rcu_quiesce();
        gc_epoch_ = 0;
    }
    void rcu_quiesce() {
        rcu_start();
        if (perform_gc_epoch_ != active_epoch)
            hard_rcu_quiesce();
    }
    typedef ::mrcu_callback mrcu_callback;
    void rcu_register(mrcu_callback* cb) {
        record_rcu(cb, memtag(-1));
    }

    // thread management
    pthread_t& pthread() {
        return pthreadid_;
    }
    pthread_t pthread() const {
        return pthreadid_;
    }

    void report_rcu(void* ptr) const;
    static void report_rcu_all(void* ptr);
    static inline mrcu_epoch_type min_active_epoch();

  private:
    union {
        struct {
            mrcu_epoch_type gc_epoch_;
            mrcu_epoch_type perform_gc_epoch_;
            loginfo *logger_;

            threadinfo *next_;
            int purpose_;
            int index_;         // the index of a udp, logging, tcp,
                                // checkpoint or recover thread

            pthread_t pthreadid_;
        };
        char padding1[CACHE_LINE_SIZE];
    };

    enum { pool_max_nlines = 20 };
    void* pool_[pool_max_nlines];

    limbo_group* limbo_head_;
    limbo_group* limbo_tail_;
    mutable kvtimestamp_t ts_;

    //enum { ncounters = (int) tc_max };
    enum { ncounters = 0 };
    uint64_t counters_[ncounters];

    void refill_pool(int nl);
    void refill_rcu();

    void free_rcu(void *p, memtag tag) {
        if ((tag & memtag_pool_mask) == 0) {
            p = memdebug::check_free_after_rcu(p, tag);
            ::free(p);
        } else if (tag == memtag(-1))
            (*static_cast<mrcu_callback*>(p))(*this);
        else {
            p = memdebug::check_free_after_rcu(p, tag);
            int nl = tag & memtag_pool_mask;
            *reinterpret_cast<void**>(p) = pool_[nl - 1];
            pool_[nl - 1] = p;
        }
    }

    void record_rcu(void* ptr, memtag tag) {
        if (limbo_tail_->tail_ + 2 > limbo_tail_->capacity)
            refill_rcu();
        uint64_t epoch = globalepoch;
        limbo_tail_->push_back(ptr, tag, epoch);
    }

#if ENABLE_ASSERTIONS
    static int no_pool_value;
#endif
    static bool use_pool() {
#if ENABLE_ASSERTIONS
        return !no_pool_value;
#else
        return true;
#endif
    }

    inline threadinfo(int purpose, int index);
    threadinfo(const threadinfo&) = delete;
    ~threadinfo() {}
    threadinfo& operator=(const threadinfo&) = delete;

    void hard_rcu_quiesce();

    friend struct limbo_group;
};

inline mrcu_epoch_type threadinfo::min_active_epoch() {
    mrcu_epoch_type ae = globalepoch;
    for (threadinfo* ti = allthreads; ti; ti = ti->next()) {
        prefetch((const void*) ti->next());
        mrcu_epoch_type te = ti->gc_epoch_;
        if (te && mrcu_signed_epoch_type(te - ae) < 0)
            ae = te;
    }
    return ae;
}

#endif

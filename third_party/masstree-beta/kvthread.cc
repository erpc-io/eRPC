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
#include "kvthread.hh"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <new>
#include <sys/mman.h>
#if HAVE_SUPERPAGE && !NOSUPERPAGE
#include <sys/types.h>
#include <dirent.h>
#endif

threadinfo *threadinfo::allthreads;
#if ENABLE_ASSERTIONS
int threadinfo::no_pool_value;
#endif

inline threadinfo::threadinfo(int purpose, int index) {
    gc_epoch_ = perform_gc_epoch_ = 0;
    logger_ = nullptr;
    next_ = nullptr;
    purpose_ = purpose;
    index_ = index;

    for (size_t i = 0; i != sizeof(pool_) / sizeof(pool_[0]); ++i) {
        pool_[i] = nullptr;
    }

    void *limbo_space = allocate(sizeof(limbo_group), memtag_limbo);
    mark(tc_limbo_slots, limbo_group::capacity);
    limbo_head_ = limbo_tail_ = new(limbo_space) limbo_group;
    ts_ = 2;

    for (size_t i = 0; i != sizeof(counters_) / sizeof(counters_[0]); ++i) {
        counters_[i] = 0;
    }
}

threadinfo *threadinfo::make(int purpose, int index) {
    static int threads_initialized;

    threadinfo* ti = new(malloc(8192)) threadinfo(purpose, index);
    ti->next_ = allthreads;
    allthreads = ti;

    if (!threads_initialized) {
#if ENABLE_ASSERTIONS
        const char* s = getenv("_");
        no_pool_value = s && strstr(s, "valgrind") != 0;
#endif
        threads_initialized = 1;
    }

    return ti;
}

void threadinfo::refill_rcu() {
    if (!limbo_tail_->next_) {
        void *limbo_space = allocate(sizeof(limbo_group), memtag_limbo);
        mark(tc_limbo_slots, limbo_group::capacity);
        limbo_tail_->next_ = new(limbo_space) limbo_group;
    }
    limbo_tail_ = limbo_tail_->next_;
    assert(limbo_tail_->head_ == 0 && limbo_tail_->tail_ == 0);
}

inline unsigned limbo_group::clean_until(threadinfo& ti, mrcu_epoch_type epoch_bound,
                                         unsigned count) {
    epoch_type epoch = 0;
    while (head_ != tail_) {
        if (e_[head_].ptr_) {
            ti.free_rcu(e_[head_].ptr_, e_[head_].u_.tag);
            ti.mark(tc_gc);
            --count;
            if (!count) {
                e_[head_].ptr_ = nullptr;
                e_[head_].u_.epoch = epoch;
                break;
            }
        } else {
            epoch = e_[head_].u_.epoch;
            if (signed_epoch_type(epoch_bound - epoch) < 0)
                break;
        }
        ++head_;
    }
    if (head_ == tail_)
        head_ = tail_ = 0;
    return count;
}

void threadinfo::hard_rcu_quiesce() {
    limbo_group* empty_head = nullptr;
    limbo_group* empty_tail = nullptr;
    unsigned count = rcu_free_count;

    mrcu_epoch_type epoch_bound = active_epoch - 1;
    if (limbo_head_->head_ == limbo_head_->tail_
        || mrcu_signed_epoch_type(epoch_bound - limbo_head_->first_epoch()) < 0)
        goto done;

    // clean [limbo_head_, limbo_tail_]
    while (count) {
        count = limbo_head_->clean_until(*this, epoch_bound, count);
        if (limbo_head_->head_ != limbo_head_->tail_)
            break;
        if (!empty_head)
            empty_head = limbo_head_;
        empty_tail = limbo_head_;
        if (limbo_head_ == limbo_tail_) {
            limbo_head_ = limbo_tail_ = empty_head;
            goto done;
        }
        limbo_head_ = limbo_head_->next_;
    }
    // hook empties after limbo_tail_
    if (empty_head) {
        empty_tail->next_ = limbo_tail_->next_;
        limbo_tail_->next_ = empty_head;
    }

done:
    if (!count)
        perform_gc_epoch_ = epoch_bound; // do GC again immediately
    else
        perform_gc_epoch_ = epoch_bound + 1;
}

void threadinfo::report_rcu(void *ptr) const
{
    for (limbo_group *lg = limbo_head_; lg; lg = lg->next_) {
        int status = 0;
        limbo_group::epoch_type e = 0;
        for (unsigned i = 0; i < lg->capacity; ++i) {
            if (i == lg->head_)
                status = 1;
            if (i == lg->tail_) {
                status = 0;
                e = 0;
            }
            if (lg->e_[i].ptr_ == ptr)
                fprintf(stderr, "thread %d: rcu %p@%d: %s as %x @%" PRIu64 "\n",
                        index_, lg, i, status ? "waiting" : "freed",
                        lg->e_[i].u_.tag, e);
            else if (!lg->e_[i].ptr_)
                e = lg->e_[i].u_.epoch;
        }
    }
}

void threadinfo::report_rcu_all(void *ptr)
{
    for (threadinfo *ti = allthreads; ti; ti = ti->next())
        ti->report_rcu(ptr);
}


#if HAVE_SUPERPAGE && !NOSUPERPAGE
static size_t read_superpage_size() {
    if (DIR* d = opendir("/sys/kernel/mm/hugepages")) {
        size_t n = (size_t) -1;
        while (struct dirent* de = readdir(d))
            if (de->d_type == DT_DIR
                && strncmp(de->d_name, "hugepages-", 10) == 0
                && de->d_name[10] >= '0' && de->d_name[10] <= '9') {
                size_t x = strtol(&de->d_name[10], 0, 10) << 10;
                n = (x < n ? x : n);
            }
        closedir(d);
        return n;
    } else
        return 2 << 20;
}

static size_t superpage_size = 0;
#endif

static void initialize_pool(void* pool, size_t sz, size_t unit) {
    char* p = reinterpret_cast<char*>(pool);
    void** nextptr = reinterpret_cast<void**>(p);
    for (size_t off = unit; off + unit <= sz; off += unit) {
        *nextptr = p + off;
        nextptr = reinterpret_cast<void**>(p + off);
    }
    *nextptr = 0;
}

void threadinfo::refill_pool(int nl) {
    assert(!pool_[nl - 1]);

    if (!use_pool()) {
        pool_[nl - 1] = malloc(nl * CACHE_LINE_SIZE);
        if (pool_[nl - 1])
            *reinterpret_cast<void**>(pool_[nl - 1]) = 0;
        return;
    }

    void* pool = 0;
    size_t pool_size = 0;
    int r;

#if HAVE_SUPERPAGE && !NOSUPERPAGE
    if (!superpage_size)
        superpage_size = read_superpage_size();
    if (superpage_size != (size_t) -1) {
        pool_size = superpage_size;
# if MADV_HUGEPAGE
        if ((r = posix_memalign(&pool, pool_size, pool_size)) != 0) {
            fprintf(stderr, "posix_memalign superpage: %s\n", strerror(r));
            pool = 0;
            superpage_size = (size_t) -1;
        } else if (madvise(pool, pool_size, MADV_HUGEPAGE) != 0) {
            perror("madvise superpage");
            superpage_size = (size_t) -1;
        }
# elif MAP_HUGETLB
        pool = mmap(0, pool_size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
        if (pool == MAP_FAILED) {
            perror("mmap superpage");
            pool = 0;
            superpage_size = (size_t) -1;
        }
# else
        superpage_size = (size_t) -1;
# endif
    }
#endif

    if (!pool) {
        pool_size = 2 << 20;
        if ((r = posix_memalign(&pool, CACHE_LINE_SIZE, pool_size)) != 0) {
            fprintf(stderr, "posix_memalign: %s\n", strerror(r));
            abort();
        }
    }

    initialize_pool(pool, pool_size, nl * CACHE_LINE_SIZE);
    pool_[nl - 1] = pool;
}

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
#ifndef KVTEST_HH
#define KVTEST_HH
#include "json.hh"
#include "misc.hh"
#include "kvproto.hh"
#include "kvrandom.hh"
#include <vector>
#include <fstream>
#include <random>

using lcdf::Str;
using lcdf::String;
using lcdf::Json;
extern int kvtest_first_seed;
// Templated KV tests, so we can run them either client/server or linked with
// the kvd binary.

template <typename N>
inline Json& kvtest_set_time(Json& result, const lcdf::String& base, N n, double delta_t)
{
    result.set(base, n);
    if (delta_t > 0)
        result.set(base + "_per_sec", n / delta_t);
    return result;
}

template <typename N>
inline Json kvtest_set_time(const Json& result, const lcdf::String& base, N n, double delta_t) {
    Json x(result);
    kvtest_set_time(x, base, n, delta_t);
    return x;
}

template <typename C>
void kvtest_sync_rw1_seed(C &client, int seed)
{
    client.rand.seed(seed);
    double tp0 = client.now();
    unsigned n;
    for (n = 0; !client.timeout(0) && n <= client.limit(); ++n) {
        int32_t x = (int32_t) client.rand();
        client.put_sync(x, x + 1);
    }
    client.wait_all();
    double tp1 = client.now();

    client.puts_done();
    client.notice("now getting\n");
    int32_t *a = (int32_t *) malloc(sizeof(int32_t) * n);
    assert(a);
    client.rand.seed(seed);
    for (unsigned i = 0; i < n; ++i) {
        a[i] = (int32_t) client.rand();
    }
    kvrandom_uniform_int_distribution<unsigned> swapd(0, n - 1);
    for (unsigned i = 0; i < n; ++i) {
        std::swap(a[i], a[swapd(client.rand)]);
    }

    double tg0 = client.now();
    unsigned g;
    for (g = 0; g < n && !client.timeout(1); ++g) {
        client.get_check_sync(a[g], a[g] + 1);
    }
    client.wait_all();
    double tg1 = client.now();

    Json result = Json();
    kvtest_set_time(result, "puts", n, tp1 - tp0);
    kvtest_set_time(result, "gets", g, tg1 - tg0);
    kvtest_set_time(result, "ops", n + g, (tp1 - tp0) + (tg1 - tg0));
    client.report(result);
    free(a);
}

template <typename C>
void kvtest_sync_rw1(C &client)
{
    kvtest_sync_rw1_seed(client, kvtest_first_seed + client.id() % 48);
}

template <typename C>
unsigned kvtest_rw1puts_seed(C& client, int seed) {
    client.rand.seed(seed);
    double tp0 = client.now();
    unsigned n;
    for (n = 0; !client.timeout(0) && n <= client.limit(); ++n) {
        int32_t x = (int32_t) client.rand();
        client.put(x, x + 1);
    }
    client.wait_all();
    double tp1 = client.now();
    client.puts_done();

    client.report(kvtest_set_time(Json(), "puts", n, tp1 - tp0));
    return n;
}

// do a bunch of inserts to distinct keys, then check that they all showed up.
// sometimes overwrites, but only w/ same value.
// different clients might use same key sometimes.
template <typename C>
void kvtest_rw1_seed(C &client, int seed)
{
    unsigned n = kvtest_rw1puts_seed(client, seed);

    client.notice("now getting\n");
    int32_t *a = (int32_t *) malloc(sizeof(int32_t) * n);
    assert(a);
    client.rand.seed(seed);
    for (unsigned i = 0; i < n; ++i) {
        a[i] = (int32_t) client.rand();
    }
    kvrandom_uniform_int_distribution<unsigned> swapd(0, n - 1);
    for (unsigned i = 0; i < n; ++i) {
        std::swap(a[i], a[swapd(client.rand)]);
    }

    double tg0 = client.now();
    unsigned g;
#if 0
#define BATCH 8
    for(g = 0; g+BATCH < n && !client.timeout(1); g += BATCH){
      long key[BATCH], expected[BATCH];
      for(int i = 0; i < BATCH; i++){
        key[i] = a[g+i];
        expected[i] = a[g+i] + 1;
      }
      client.many_get_check(BATCH, key, expected);
    }
#else
    for (g = 0; g < n && !client.timeout(1); ++g) {
        client.get_check(a[g], a[g] + 1);
    }
#endif
    client.wait_all();
    double tg1 = client.now();

    Json result = client.report(Json());
    kvtest_set_time(result, "gets", g, tg1 - tg0);
    double delta_puts = n / result["puts_per_sec"].as_d();
    kvtest_set_time(result, "ops", n + g, delta_puts + (tg1 - tg0));
    client.report(result);
    free(a);
}

template <typename C>
void kvtest_rw1puts(C &client)
{
    kvtest_rw1puts_seed(client, kvtest_first_seed + client.id() % 48);
}

template <typename C>
void kvtest_rw1(C &client)
{
    kvtest_rw1_seed(client, kvtest_first_seed + client.id() % 48);
}

// do a bunch of inserts to distinct keys, then check that they all showed up.
// sometimes overwrites, but only w/ same value.
// different clients might use same key sometimes.
template <typename C>
void kvtest_rw1long_seed(C &client, int seed)
{
    const char * const formats[] = {
        "user%u", "machine%u", "opening%u", "fartparade%u"
    };
    char buf[64];

    client.rand.seed(seed);
    double tp0 = client.now();
    unsigned n;
    kvrandom_uniform_int_distribution<unsigned> fmtd(0, 3);
    for (n = 0; !client.timeout(0) && n <= client.limit(); ++n) {
        int32_t x = (int32_t) client.rand();
        client.put(Str::snprintf(buf, sizeof(buf), formats[fmtd(client.rand)], x), x + 1);
    }
    client.wait_all();
    double tp1 = client.now();

    client.puts_done();
    client.notice("now getting\n");
    int32_t *a = (int32_t *) malloc(sizeof(int32_t) * n * 2);
    assert(a);
    client.rand.seed(seed);
    for (unsigned i = 0; i < n * 2; ++i) {
        a[i] = (int32_t) client.rand();
    }
    kvrandom_uniform_int_distribution<unsigned> swapd(0, n - 1);
    for (unsigned i = 0; i < n; ++i) {
        unsigned x = swapd(client.rand);
        std::swap(a[2 * i], a[2 * x]);
        std::swap(a[2 * i + 1], a[2 * x + 1]);
    }

    double tg0 = client.now();
    unsigned g;
    for (g = 0; g < n && !client.timeout(1); ++g) {
        unsigned fmt = a[2 * g];
        int32_t x = (int32_t) a[2 * g + 1];
        client.get_check(Str::snprintf(buf, sizeof(buf), formats[fmt % 4], x), x + 1);
    }
    client.wait_all();
    double tg1 = client.now();

    Json result = Json();
    kvtest_set_time(result, "puts", n, tp1 - tp0);
    kvtest_set_time(result, "gets", g, tg1 - tg0);
    kvtest_set_time(result, "ops", n + g, (tp1 - tp0) + (tg1 - tg0));
    client.report(result);
    free(a);
}

template <typename C>
void kvtest_rw1long(C &client)
{
    kvtest_rw1long_seed(client, kvtest_first_seed + client.id() % 48);
}

// interleave inserts and gets for random keys.
template <typename C>
void kvtest_rw2_seed(C &client, int seed, double getfrac)
{
    client.rand.seed(seed);
    const unsigned c = 2654435761U;
    const unsigned offset = client.rand();

    double t0 = client.now();
    uint64_t puts = 0, gets = 0;
    kvrandom_bernoulli_distribution getd(getfrac);
    while (!client.timeout(0) && (puts + gets) <= client.limit()) {
        if (puts == 0 || !getd(client.rand)) {
            // insert
            unsigned x = (offset + puts) * c;
            client.put(x, x + 1);
            ++puts;
        } else {
            // get
            unsigned x = (offset + (client.rand() % puts)) * c;
            client.get_check(x, x + 1);
            ++gets;
        }
    }
    client.wait_all();
    double t1 = client.now();

    Json result = Json().set("puts", puts).set("gets", gets);
    kvtest_set_time(result, "ops", puts + gets, t1 - t0);
    client.report(result);
}

template <typename C>
void kvtest_rw2(C &client)
{
    kvtest_rw2_seed(client, kvtest_first_seed + client.id() % 48, 0.5);
}

template <typename C>
void kvtest_rw2g90(C &client)
{
    kvtest_rw2_seed(client, kvtest_first_seed + client.id() % 48, 0.9);
}

template <typename C>
void kvtest_rw2g98(C &client)
{
    kvtest_rw2_seed(client, kvtest_first_seed + client.id() % 48, 0.98);
}

// interleave inserts and gets for random keys.
template <typename C>
void kvtest_rw2fixed_seed(C &client, int seed, double getfrac)
{
    client.rand.seed(seed);
    const unsigned c = 2654435761U;
    const unsigned offset = client.rand();

    double t0 = client.now();
    uint64_t puts = 0, gets = 0;
    kvrandom_bernoulli_distribution getd(getfrac);
    while (!client.timeout(0) && (puts + gets) <= client.limit()) {
        if (puts == 0 || !getd(client.rand)) {
            // insert
            unsigned x = (offset + puts) * c;
            x %= 100000000;
            client.put(x, x + 1);
            ++puts;
        } else {
            // get
            unsigned x = (offset + (client.rand() % puts)) * c;
            x %= 100000000;
            client.get_check(x, x + 1);
            ++gets;
        }
    }
    client.wait_all();
    double t1 = client.now();

    Json result = Json().set("puts", puts).set("gets", gets);
    kvtest_set_time(result, "ops", puts + gets, t1 - t0);
    client.report(result);
}

template <typename C>
void kvtest_rw2fixed(C &client)
{
    kvtest_rw2fixed_seed(client, kvtest_first_seed + client.id() % 48, 0.5);
}

template <typename C>
void kvtest_rw2fixedg90(C &client)
{
    kvtest_rw2fixed_seed(client, kvtest_first_seed + client.id() % 48, 0.9);
}

template <typename C>
void kvtest_rw2fixedg98(C &client)
{
    kvtest_rw2fixed_seed(client, kvtest_first_seed + client.id() % 48, 0.98);
}

// do a bunch of inserts to sequentially increasing keys,
// then check that they all showed up.
// different clients might use same key sometimes.
template <typename C>
void kvtest_rw3(C &client)
{
    double t0 = client.now();
    uint64_t n;
    for (n = 0; !client.timeout(0) && n <= client.limit(); ++n) {
        client.put_key8(n, n + 1);
    }
    client.wait_all();

    client.puts_done();
    client.notice("now getting\n");

    double t1 = client.now();
    for (unsigned i = 0; i < n; ++i) {
        client.get_check_key8(i, i + 1);
    }
    client.wait_all();

    double t2 = client.now();

    Json result = Json();
    kvtest_set_time(result, "puts", n, t1 - t0);
    kvtest_set_time(result, "gets", n, t2 - t1);
    kvtest_set_time(result, "ops", n + n, t2 - t0);
    client.report(result);
}

// do a bunch of inserts to sequentially decreasing keys,
// then check that they all showed up.
// different clients might use same key sometimes.
template <typename C>
void kvtest_rw4(C &client)
{
    const int top = 2147483647;

    double t0 = client.now();
    unsigned n;
    for (n = 0; !client.timeout(0) && n <= client.limit(); ++n) {
        client.put_key8(top - n, n + 1);
    }
    client.wait_all();

    client.puts_done();
    client.notice("now getting\n");

    double t1 = client.now();
    for (unsigned i = 0; i < n; ++i) {
        client.get_check_key8(top - i, i + 1);
    }
    client.wait_all();

    double t2 = client.now();

    Json result = Json();
    kvtest_set_time(result, "puts", n, t1 - t0);
    kvtest_set_time(result, "gets", n, t2 - t1);
    kvtest_set_time(result, "ops", n + n, t2 - t0);
    client.report(result);
}

// do a bunch of inserts to sequentially decreasing 8B keys,
// then check that they all showed up.
// different clients might use same key sometimes.
template <typename C>
void kvtest_rw4fixed(C &client)
{
    const int top = 99999999;

    double t0 = client.now();
    unsigned n;
    for (n = 0; !client.timeout(0) && n <= client.limit(); ++n) {
        client.put_key8(top - n, n + 1);
    }
    client.wait_all();

    client.puts_done();
    client.notice("now getting\n");

    double t1 = client.now();
    for (unsigned i = 0; i < n; ++i) {
        client.get_check_key8(top - i, i + 1);
    }
    client.wait_all();

    double t2 = client.now();

    Json result = Json();
    kvtest_set_time(result, "puts", n, t1 - t0);
    kvtest_set_time(result, "gets", n, t2 - t1);
    kvtest_set_time(result, "ops", n + n, t2 - t0);
    client.report(result);
}

// update the same small set of keys over and over,
// to uncover concurrent update bugs in the server.
template <typename C>
void kvtest_same_seed(C &client, int seed)
{
    client.rand.seed(seed);

    double t0 = client.now();
    unsigned n;
    kvrandom_uniform_int_distribution<unsigned> uid(0, 9);
    for (n = 0; !client.timeout(0) && n <= client.limit(); ++n) {
        unsigned x = uid(client.rand);
        client.put(x, x + 1);
    }
    client.wait_all();
    double t1 = client.now();

    Json result = Json();
    kvtest_set_time(result, "puts", n, t1 - t0);
    client.report(result);
}

template <typename C>
void kvtest_same(C &client)
{
    kvtest_same_seed(client, kvtest_first_seed + client.id() % 48);
}

// update the same small set of keys over and over, with interspersed gets.
template <typename C>
void kvtest_rwsmall_seed(C &client, int nkeys, int seed)
{
    client.rand.seed(seed);

    double t0 = client.now();
    unsigned n;
    kvrandom_uniform_int_distribution<unsigned> uid(0, (nkeys << 3) - 1);
    for (n = 0; !client.timeout(0) && n <= client.limit(); ++n) {
        unsigned x = uid(client.rand);
        if (x & 7) {
            client.get(x >> 3);
        } else {
            client.put(x >> 3, n);
        }
    }
    client.wait_all();
    double t1 = client.now();

    Json result = Json();
    kvtest_set_time(result, "ops", n, t1 - t0);
    client.report(result);
}

template <typename C>
void kvtest_rwsmall24(C &client)
{
    kvtest_rwsmall_seed(client, 24, kvtest_first_seed + client.id() % 48);
}

// update the same small set of keys over and over, with interspersed gets.
// but ensure that the keys are all on different cache lines.
template <typename C>
void kvtest_rwsep_seed(C &client, int nkeys, int clientid, int seed)
{
    for (int n = clientid * (32 + nkeys); n < (clientid + 1) * (32 + nkeys); ++n) {
        client.put(1000000 + n, n);
    }

    client.rand.seed(seed);

    double t0 = client.now();
    unsigned n;
    kvrandom_uniform_int_distribution<unsigned> uid(0, (nkeys << 3) - 1);
    for (n = 0; !client.timeout(0) && n <= client.limit(); ++n) {
        unsigned x = uid(client.rand);
        if (x & 7) {
            client.get(1000000 + clientid * (32 + nkeys) + (x >> 3));
        } else {
            client.put(1000000 + clientid * (32 + nkeys) + (x >> 3), n);
        }
    }
    client.wait_all();
    double t1 = client.now();

    Json result = Json();
    kvtest_set_time(result, "ops", n, t1 - t0);
    client.report(result);
}

template <typename C>
void kvtest_rwsep24(C &client)
{
    kvtest_rwsep_seed(client, 24, client.id(), kvtest_first_seed + client.id() % 48);
}

// Same as rw1, except that the keys are no more than 8 bytes
template <typename C>
void kvtest_rw1fixed_seed(C &client, int seed)
{
    client.rand.seed(seed);
    double tp0 = client.now();
    unsigned n;
    kvrandom_uniform_int_distribution<unsigned> uid(0, 99999999);
    for (n = 0; !client.timeout(0) && n <= client.limit(); ++n) {
        int32_t x = uid(client.rand);
        client.put(x, x + 1);
    }
    client.wait_all();
    double tp1 = client.now();

    client.puts_done();
    client.notice("now getting\n");
    int32_t *a = (int32_t *) malloc(sizeof(int32_t) * n);
    assert(a);
    client.rand.seed(seed);
    for (unsigned i = 0; i < n; ++i) {
        a[i] = uid(client.rand);
    }
    kvrandom_uniform_int_distribution<unsigned> swapd(0, n - 1);
    for (unsigned i = 0; i < n; ++i) {
        std::swap(a[i], a[swapd(client.rand)]);
    }

    double tg0 = client.now();
    unsigned g;
#if 0
#define BATCH 8
    for(g = 0; g+BATCH < n && !client.timeout(1); g += BATCH){
      long key[BATCH], expected[BATCH];
      for(int i = 0; i < BATCH; i++){
        key[i] = a[g+i];
        expected[i] = a[g+i] + 1;
      }
      client.many_get_check(BATCH, key, expected);
    }
#else
    for (g = 0; g < n && !client.timeout(1); ++g) {
        client.get_check(a[g], a[g] + 1);
    }
#endif
    client.wait_all();
    double tg1 = client.now();

    Json result = Json();
    kvtest_set_time(result, "puts", n, tp1 - tp0);
    kvtest_set_time(result, "gets", g, tg1 - tg0);
    kvtest_set_time(result, "ops", n + g, (tp1 - tp0) + (tg1 - tg0));
    client.report(result);
    free(a);
}

template <typename C>
void kvtest_rw1fixed(C &client)
{
    kvtest_rw1fixed_seed(client, kvtest_first_seed + client.id() % 48);
}

// Same as rw1, except that keys are 16-bytes (prefixed with "0"s)
template <typename C>
void kvtest_rw16_seed(C &client, int seed)
{
    client.rand.seed(seed);
    double tp0 = client.now();
    int n;
    char key[256];
    char val[256];
    for (n = 0; !client.timeout(0); ++n) {
        int32_t x = (int32_t) client.rand();
        sprintf(key, "%016d", x);
        sprintf(val, "%016d", x + 1);
        client.put(key, val);
    }
    client.wait_all();
    double tp1 = client.now();

    client.puts_done();
    client.notice("now getting\n");
    int32_t *a = (int32_t *) malloc(sizeof(int32_t) * n);
    assert(a);
    client.rand.seed(seed);
    for (int i = 0; i < n; ++i) {
        a[i] = (int32_t) client.rand();
    }
    kvrandom_uniform_int_distribution<unsigned> swapd(0, n - 1);
    for (int i = 0; i < n; ++i) {
        std::swap(a[i], a[swapd(client.rand)]);
    }

    double tg0 = client.now();
    int g;
    for (g = 0; g < n && !client.timeout(1); ++g) {
        sprintf(key, "%016d", a[g]);
        sprintf(val, "%016d", a[g] + 1);
        client.get_check(key, val);
    }
    client.wait_all();
    double tg1 = client.now();

    Json result = Json();
    kvtest_set_time(result, "puts", n, tp1 - tp0);
    kvtest_set_time(result, "gets", g, tg1 - tg0);
    kvtest_set_time(result, "ops", n + g, (tp1 - tp0) + (tg1 - tg0));
    client.report(result);
    free(a);
}

template <typename C>
void kvtest_rw16(C &client)
{
    kvtest_rw16_seed(client, kvtest_first_seed + client.id() % 48);
}


// A writer and a deleter; the deleter chases the writer
template <typename C>
void kvtest_wd1(unsigned initial_pos, int incr, C &client)
{
    incr = std::max(incr, client.nthreads() / 2);
    unsigned pos = initial_pos + ((client.id() / 2) % incr);
    unsigned n = 0;
    Json result = Json();

    double t0 = client.now();
    if (client.id() % 2) {
        while (!client.get_sync(pos + 16 * incr)) {
            /* spin */
        }
        while (!client.timeout(0) && n <= client.limit()) {
            ++n;
            if (client.remove_sync(pos)) {
                pos += incr;
            }
            if ((n % (1 << 6)) == 0) {
                client.rcu_quiesce();
            }
        }
        result.set("removepos", pos);
    } else {
        while (!client.timeout(0) && n <= client.limit()) {
            ++n;
            client.put(pos, pos + 1);
            pos += incr;
            if ((n % (1 << 6)) == 0) {
                client.rcu_quiesce();
            }
        }
        result.set("putpos", pos);
    }
    client.wait_all();
    double t1 = client.now();

    kvtest_set_time(result, "ops", n, t1 - t0);
    client.report(result);
}

template <typename C>
void kvtest_wd1_check(unsigned initial_pos, int incr, C &client)
{
    incr = std::max(incr, client.nthreads() / 2);
    unsigned pos = initial_pos + ((client.id() / 2) % incr);
    unsigned n = 0;
    Json result = Json();

    double t0 = client.now();
    if (client.id() % 2 == 0) {
        unsigned max_remove = -1, min_post_remove = -1, max_post_remove = -1;
        unsigned bugs = 0;
        bool found_putpos = false;
        constexpr int nbatch = 20;
        Str gotten[nbatch];
        char gottenbuf[nbatch * 16];
        for (int i = 0; i < nbatch; ++i) {
            gotten[i].s = &gottenbuf[i * 16];
        }

        while (!client.timeout(0)
               && (!found_putpos || pos < max_post_remove + 100000)) {
            for (int i = 0; i < nbatch; ++i) {
                gotten[i].len = 16;
                client.get(pos + i * incr, &gotten[i]);
            }
            client.wait_all();
            for (int i = 0; i < nbatch; ++i) {
                if (gotten[i].len) {
                    if (min_post_remove == unsigned(-1)) {
                        min_post_remove = max_post_remove = pos;
                    } else if (!found_putpos) {
                        max_post_remove = pos;
                    } else if (++bugs == 1) {
                        fprintf(stderr, "%u: present unexpectedly\n", pos);
                    }
                } else {
                    if (min_post_remove == unsigned(-1)) {
                        max_remove = pos;
                    } else {
                        found_putpos = true;
                    }
                }
                pos += incr;
            }
        }

        result.set("removepos", max_remove + incr);
        result.set("putpos", max_post_remove + incr);
        if (bugs) {
            result.set("buggykeys", bugs);
        }
    }
    client.wait_all();
    double t1 = client.now();

    kvtest_set_time(result, "ops", n, t1 - t0);
    client.report(result);
}

template <typename C>
void kvtest_wd2(C &client)
{
    char sbuf[32], kbuf[32], next_kbuf[32];
    const int sep = 26;
    const int p_remove = 1000, p_put2 = 10000, p_remove2 = 20000;
    int x = 0;
    quick_istr xstr(0);

    client.put(Str("n"), client.nthreads());
    always_assert(client.nthreads() > 1);

    // set up status keys
    snprintf(sbuf, sizeof(sbuf), "s%03d", client.id());
    for (int i = 0; i < sep; ++i) {
        sbuf[4] = 'A' + i;
        client.put(Str(sbuf, 5), Str());
    }
    client.put(Str(sbuf, 4), xstr.string());

    // set up main keys
    snprintf(kbuf, sizeof(kbuf), "k%03d", client.id());
    for (int i = 0; i < sep; ++i) {
        kbuf[4] = 'A' + i;
        client.put(Str(kbuf, 5), Str());
    }
    client.put(Str(kbuf, 4), Str());

    snprintf(next_kbuf, sizeof(next_kbuf), "k%03d", (client.id() + 1) % client.nthreads());

    // main loop
    double t0 = client.now();
    int put_status = 0;
    long nrounds = 0;
    while (!client.timeout(0)) {
        ++nrounds;
        client.put(Str(kbuf, 4), xstr.string(), &put_status);
        if ((client.rand() % 65536) < p_remove) {
            client.remove(Str(next_kbuf, 4));
        }

        int rand = client.rand() % 65536;
        if (rand < p_put2) {
            for (int i = sep - 1; i >= 0; --i) {
                next_kbuf[4] = 'A' + i;
                client.put(Str(next_kbuf, 5), Str());
            }
        } else if (rand < p_remove2) {
            for (int i = sep - 1; i >= 0; --i) {
                next_kbuf[4] = 'A' + i;
                client.remove(Str(next_kbuf, 5));
            }
        } else {
            /* do nothing */
        }

        client.wait_all();

        if (put_status == Inserted) {
            ++x;
            xstr.set(x);
            client.put(Str(sbuf, 4), xstr.string());
        }
    }
    double t1 = client.now();

    Json result;
    kvtest_set_time(result, "rounds", nrounds, t1 - t0);
    client.report(result);
}

template <typename C>
void kvtest_wd2_check(C &client)
{
    if (client.id() != 0) {
        return;
    }

    int n;
    client.get(Str("n"), &n);
    client.wait_all();
    always_assert(n > 1);
    Json result;

    char buf[32];
    for (int i = 0; i < n; ++i) {
        int s, k;
        snprintf(buf, sizeof(buf), "k%03d", i);
        client.get(Str(buf, 4), &k);
        snprintf(buf, sizeof(buf), "s%03d", i);
        client.get(Str(buf, 4), &s);
        client.wait_all();
        if (!(s >= 0 && (s == k || s == k + 1 || k == -1))) {
            fprintf(stderr, "problem: s%03d=%d vs. k%03d=%d\n",
                    i, s, i, k);
        }
        result.set("thread" + String(i), Json().push_back(s).push_back(k));
    }

    client.report(result);
}

template <typename C>
void kvtest_wd3(C& client, uint64_t nk_total)
{
    if (client.has_param("rangesize")) {
        nk_total = client.param("rangesize").to_u64() * client.nthreads();
    } else if (client.has_param("nkeys")) {
        nk_total = client.param("nkeys").to_u64();
    }
    uint64_t nk = (nk_total + client.nthreads() - 1) / client.nthreads();
    quick_istr k0(nk * client.id(), 8);

    String prefix = client.param("prefix").to_s();
    size_t plen = prefix.length();
    char buf[128];
    always_assert(plen + k0.length() < sizeof(buf));
    memcpy(buf, prefix.data(), plen);
    char* ebuf = buf + plen + k0.length();

    double t0 = client.now();
    unsigned long nrounds = 0;
    while (!client.timeout(0)) {
        ++nrounds;
        memcpy(ebuf - k0.length(), k0.data(), k0.length());
        for (uint64_t i = nk; i != 0 && !client.timeout(0); --i) {
            client.insert_check(Str(buf, ebuf), Str(ebuf - 8, ebuf));
            quick_istr::increment_from_end(ebuf);
        }

        memcpy(ebuf - k0.length(), k0.data(), k0.length());
        for (uint64_t i = nk; i != 0 && !client.timeout(0); --i) {
            client.get_check(Str(buf, ebuf), Str(ebuf - 8, ebuf));
            quick_istr::increment_from_end(ebuf);
        }

        memcpy(ebuf - k0.length(), k0.data(), k0.length());
        for (uint64_t i = nk; i != 0 && !client.timeout(0); --i) {
            client.remove_check(Str(buf, ebuf));
            quick_istr::increment_from_end(ebuf);
        }

        memcpy(ebuf - k0.length(), k0.data(), k0.length());
        for (uint64_t i = nk; i != 0 && !client.timeout(0); --i) {
            client.get_check_absent(Str(buf, ebuf));
            quick_istr::increment_from_end(ebuf);
        }
    }
    client.wait_all();
    double t1 = client.now();

    Json result;
    result.set("rangesize", nk);
    memcpy(ebuf - k0.length(), k0.data(), k0.length());
    result.set("first", Str(buf, ebuf));
    kvtest_set_time(result, "rounds", nrounds, t1 - t0);
    kvtest_set_time(result, "ops", nrounds * nk * 4, t1 - t0);
    client.report(result);
}

template <typename C>
void kvtest_conflictscan1(C& client)
{
    unsigned prefixlen = client.param("prefixlen", 4).to_u64();
    unsigned rangelen = client.param("rangelen", 4).to_u64();
    unsigned keylen = prefixlen + rangelen;
    unsigned rangesize = client.param("rangesize", 100).to_u64();
    unsigned scansize = client.param("scansize", 20).to_u64();
    assert(rangelen > 0 && rangelen <= 8);
    assert(prefixlen > 0 && prefixlen <= 8);
    assert(rangelen == 8 || rangesize < (1UL << (rangelen * 8)));
    assert(scansize < rangesize);
    using leaf_type = typename C::table_type::leaf_type;

    union {
        uint64_t u;
        char c[8];
    } x;

    if (client.id() == 0) {
        // scan worker
        char hbuf[16];
        uint64_t nscans = 0, naborts = 0;
        kvrandom_uniform_int_distribution<uint64_t> ntd(1, client.nthreads() - 1);
        std::vector<Str> keys, values;
        client.wait_all();

        while (!client.timeout(0)) {
            ++nscans;
            memset(hbuf, 0, 16);
            x.u = host_to_net_order(ntd(client.rand));
            memcpy(hbuf, x.c + (8 - prefixlen), prefixlen);
            while (!client.timeout(0)) {
                client.scan_versions_sync(Str(hbuf, keylen), scansize, keys, values);
                for (size_t i = 0; i != client.scan_versions().size(); i += 2) {
                    leaf_type* l = reinterpret_cast<leaf_type*>(client.scan_versions()[i]);
                    uint64_t uv = l->full_unlocked_version_value();
                    if (uv != client.scan_versions()[i + 1]) {
                        goto abort;
                    }
                }
                break;
            abort:
                ++naborts;
            }
        }
        Json result;
        result.set("keylen", keylen).set("prefixlen", prefixlen).set("scansize", scansize).set("scans", nscans).set("aborts", naborts);
        client.report(result);

    } else {
        // insert/delete worker
        char hbuf[16], tbuf[16];
        x.u = host_to_net_order(uint64_t(client.id()));
        memcpy(hbuf, x.c + (8 - prefixlen), prefixlen);
        memset(hbuf + prefixlen, 0, rangelen);
        memcpy(tbuf, hbuf, 16);
        char lastp = tbuf[prefixlen - 1];

        // set initial values
        for (unsigned i = 0; i != rangesize; ++i) {
            client.insert_check(Str(tbuf, keylen), Str(tbuf, 8));
            quick_istr::binary_increment_from_end(tbuf + keylen);
        }
        client.wait_all();

        // insert/delete
        uint64_t ninsert = 0, nremove = 0, cursize = rangesize;
        while (cursize && !client.timeout(0)) {
            if (tbuf[prefixlen - 1] == lastp
                && cursize < rangesize * 2
                && (cursize == scansize
                    || client.rand() % 65536 < 32768)) {
                client.insert_check(Str(tbuf, keylen), Str(tbuf + keylen - 8, 8));
                quick_istr::binary_increment_from_end(tbuf + keylen);
                ++ninsert;
                ++cursize;
            } else {
                client.remove_check(Str(hbuf, keylen));
                quick_istr::binary_increment_from_end(hbuf + keylen);
                ++nremove;
                --cursize;
            }
        }
        Json result;
        result.set("rangesize", rangesize).set("inserts", ninsert).set("removes", nremove);
        client.report(result);
    }
}

// Create a range of keys [initial_pos, initial_pos + n)
// where key k == initial_pos + i has value (n - 1 - i).
// Many overwrites.
template <typename C>
void kvtest_tri1(unsigned initial_pos, int incr, C &client)
{
    incr = std::max(incr, client.nthreads());
    unsigned n = 0;
    Json result = Json();

    double t0 = client.now();
    for (unsigned x = 0; x < client.limit(); ++x) {
        for (unsigned y = 0, z = x; y <= x; ++y, --z, ++n) {
            client.put(initial_pos + y * incr, z);
        }
    }
    client.wait_all();
    double t1 = client.now();

    kvtest_set_time(result, "puts", n, t1 - t0);
    kvtest_set_time(result, "ops", n, t1 - t0);
    client.report(result);
}

template <typename C>
void kvtest_tri1_check(unsigned initial_pos, int incr, C &client)
{
    incr = std::max(incr, client.nthreads());
    unsigned n = 0;
    Json result = Json();

    double t0 = client.now();
    for (unsigned x = 0; x < client.limit(); ++x, ++n) {
        client.get_check(initial_pos + x * incr, client.limit() - 1 - x);
    }
    client.wait_all();
    double t1 = client.now();

    kvtest_set_time(result, "gets", n, t1 - t0);
    kvtest_set_time(result, "ops", n, t1 - t0);
    client.report(result);
}


#define PALMN   128000000
enum { PalmBatch = 8192 / 24 };
#define PALM_DEBUG 1    // use get_check in palmb, which force palm::get
                        // to touch the cachline of the value
template <typename C>
void kvtest_palma(C &client)
{
    Json result = Json();
    double t0 = client.now();
    for (int i = 0; i < PALMN; i++) {
        uint64_t v = i + 1;
        client.put(i, v);
    }
    client.wait_all();
    double t1 = client.now();
    kvtest_set_time(result, "ops", PALMN, t1 - t0);
    client.report(result);
}

inline int compare_int(const void *a, const void *b)
{
    return compare(*(uint64_t *)a, *(uint64_t *)b);
}

template <typename C>
void kvtest_palmb_seed(C &client, int seed)
{
    Json result = Json();
    client.rand.seed(seed);
    double t0 = client.now();
    int n;
    int nquery = 0;
    uint64_t a[PalmBatch];
    for (n = 0; !client.timeout(0); ++n) {
        uint64_t x = (uint64_t) client.rand();
        x %= (PALMN / 10);
        a[nquery++] = x;
        if (nquery == PalmBatch) {
            qsort(a, PalmBatch, sizeof(a[0]), compare_int);
            for (int j = 0; j < PalmBatch && !client.timeout(0); j++) {
#if PALM_DEBUG
                uint64_t v = a[j] + 1;
                client.get_check(a[j], v);
#else
                client.get(a[j]);
#endif
            }
            nquery = 0;
        }
    }
    client.wait_all();
    double t1 = client.now();
    kvtest_set_time(result, "ops", n, t1 - t0);
    client.report(result);
}

template <typename C>
void kvtest_palmb(C &client)
{
    kvtest_palmb_seed(client, kvtest_first_seed + client.id() % 48);
}

template <typename C>
void kvtest_ycsbk_seed(C &client, int seed)
{
    client.rand.seed(seed);
    double tp0 = client.now();
    int n;
    char key[512], val[512];
    for (n = 0; !client.timeout(0) && n < 1000000; ++n) {
        strcpy(key, "user");
        int p = 4;
        for (int i = 0; i < 18; i++, p++) {
            key[p] = '0' + (client.rand() % 10);
        }
        key[p] = 0;
        int32_t v = (int32_t) client.rand();
        sprintf(val, "%d", v);
        client.put(Str(key, strlen(key)), Str(val, strlen(val)));
    }
    client.wait_all();
    double tp1 = client.now();

    client.puts_done();
    client.notice("now getting\n");
    client.rand.seed(seed);
    double tg0 = client.now();
    int g;
    for (g = 0; g < n && !client.timeout(1); ++g) {
        strcpy(key, "user");
        int p = 4;
        for (int i = 0; i < 18; i++, p++) {
            key[p] = '0' + (client.rand() % 10);
        }
        key[p] = 0;
        int32_t v = (int32_t) client.rand();
        sprintf(val, "%d", v);
        client.get_check(Str(key, strlen(key)), Str(val, strlen(val)));
    }
    client.wait_all();
    double tg1 = client.now();

    Json result = Json();
    kvtest_set_time(result, "puts", n, tp1 - tp0);
    kvtest_set_time(result, "gets", g, tg1 - tg0);
    kvtest_set_time(result, "ops", n + g, (tp1 - tp0) + (tg1 - tg0));
    client.report(result);
}

template <typename C>
void kvtest_ycsbk(C &client)
{
    kvtest_ycsbk_seed(client, kvtest_first_seed + client.id() % 48);
}

template <typename C>
void
kvtest_bdb(C &client)
{
    enum { nrec = 500000, keylen = 8, datalen = 32 };
    char key[keylen + 1];
    char val[datalen + 1];
    memset(val, '^', sizeof(val));
    val[datalen] = 0;
    key[keylen] = 0;
    srandom(0);
    for (int n = 0; n < nrec; n++) {
        for (int i = 0; i < keylen; i++) {
            key[i] = 'a' + random() % 26;
        }
        client.put(key, val);
    }
    client.wait_all();

    srandom(0);
    double t0 = now();
    unsigned long n;
    for (n = 0; n < 10000000; n++) {
        for (int i = 0; i < keylen; i++) {
            key[i] = 'a' + random() % 26;
        }
        client.get_check(key, val);
        if (n % nrec == 0) {
            srandom(0);
        }
    }
    double t1 = now();
    Json result = Json();
    kvtest_set_time(result, "ops", n, t1 - t0);
    client.report(result);
}

enum { NLongParts = 16 };

template <typename C>
void
kvtest_long_init(C &client)
{
    assert(client.id() < NLongParts);
    int seed = kvtest_first_seed + client.id();
    client.rand.seed(seed);
    const int keylen = client.keylen();
    const int prefixLen = client.prefixLen();
    const char minkltr = client.minkeyletter();
    const char maxkltr = client.maxkeyletter();
    assert(prefixLen < keylen);
    const uint32_t nkeysPerPart = client.nkeys() / NLongParts;
    char key[512], val[512];
    val[8] = 0;
    memset(key, '^', prefixLen);
    double t0 = now();
    unsigned long n;
    for(n = 0; n < nkeysPerPart; ++n){
        for (int i = prefixLen; i < keylen; i++) {
            key[i] = minkltr + client.rand() % (maxkltr - minkltr + 1);
        }
        key[keylen] = 0;
        memcpy(val, key + keylen - 8, 8);
        client.put(key, val);
        client.rand();
    }
    client.wait_all();
    double t1 = now();

    Json result = Json();
    kvtest_set_time(result, "puts", n, t1 - t0);
    client.report(result);
}

template <typename C>
void
kvtest_long_go(C &client)
{
    const int keylen = client.keylen();
    const int prefixLen = client.prefixLen();
    assert(prefixLen < keylen);
    const uint32_t nKeysPerPart = client.nkeys() / NLongParts;
    const char minkltr = client.minkeyletter();
    const char maxkltr = client.maxkeyletter();
    char key[512], val[512];
    memset(key, '^', prefixLen);
    val[8] = 0;
    double t0 = now();
    long n = 0;
    int cur_cid = client.id() % NLongParts;
    while (!client.timeout(0)) {
        client.rand.seed(kvtest_first_seed + cur_cid);
        uint32_t op;
        for(op = 0; !client.timeout(0) && op < nKeysPerPart; op++){
            for (int i = prefixLen; i < keylen; i++) {
                key[i] = minkltr + client.rand() % (maxkltr - minkltr + 1);
            }
            memcpy(val, key + keylen - 8, 8);
            key[keylen] = 0;
            if (client.rand() % 100 < client.getratio()) {
                client.get_check(key, val);
            } else {
                client.put(key, val);
            }
        }
        cur_cid = (cur_cid + 1) % NLongParts;
        n += op;
    }
    client.wait_all();
    double t1 = now();

    Json result = Json();
    kvtest_set_time(result, "ops", n, t1 - t0);
    client.report(result);
}

template <typename C>
void
kvtest_wscale(C &client)
{
    double t0 = now();
    client.rand.seed(kvtest_first_seed + client.id() % 48);
    long n;
    for(n = 0; !client.timeout(0); n++){
        long x = client.rand();
        client.put(x, x + 1);
    }
    client.wait_all();
    double t1 = now();
    Json result = Json();
    kvtest_set_time(result, "puts", n, t1 -t0);
    client.report(result);
}

template <typename C>
void
kvtest_ruscale_init(C &client)
{
    double t0 = now();
    client.rand.seed(kvtest_first_seed + client.id() % 48);
    const int ruscale_partsz = client.ruscale_partsz();
    const int firstkey = ruscale_partsz * client.ruscale_init_part_no();
    // Insert in random order
    int *keys = (int *) malloc(sizeof(int) * ruscale_partsz);
    always_assert(keys);
    for(int i = 0; i < ruscale_partsz; i++) {
        keys[i] = i + firstkey;
    }
    for(int i = 0; i < ruscale_partsz; i++) {
        std::swap(keys[i], keys[client.rand() % ruscale_partsz]);
    }
    for(int i = 0; i < ruscale_partsz; i++){
        long x = keys[i];
        client.put(x, x + 1);
    }
    client.wait_all();
    double t1 = now();
    Json result = Json();
    kvtest_set_time(result, "puts", ruscale_partsz, t1 - t0);
    client.report(result);
    free(keys);
}

template <typename C>
void
kvtest_rscale(C &client)
{
    client.rand.seed(kvtest_first_seed + client.id() % 48);
    const long nseqkeys = client.nseqkeys();
    double t0 = now();
    long n;
    for(n = 0; !client.timeout(0); n++){
        long x = client.rand() % nseqkeys;
        client.get_check(x, x + 1);
    }
    client.wait_all();
    double t1 = now();
    Json result = Json();
    kvtest_set_time(result, "gets", n, t1 - t0);
    client.report(result);
}

template <typename C>
void
kvtest_uscale(C &client)
{
    client.rand.seed(kvtest_first_seed + client.id());
    const long nseqkeys = client.nseqkeys();
    double t0 = now();
    long n;
    for(n = 0; !client.timeout(0); n++){
        long x = client.rand() % nseqkeys;
        client.put(x, x + 1);
    }
    client.wait_all();
    double t1 = now();
    Json result = Json();
    kvtest_set_time(result, "puts", n, t1 - t0);
    client.report(result);
}

template <typename C>
void kvtest_udp1_seed(C &client, int seed)
{
    client.rand.seed(seed);
    double tp0 = client.now();
    unsigned n;
    for (n = 0; !client.timeout(0); ++n) {
        client.put(0, 1);
    }
    client.wait_all();
    double tp1 = client.now();

    client.puts_done();
    client.notice("now getting\n");
    int32_t *a = (int32_t *) malloc(sizeof(int32_t) * n);
    assert(a);
    client.rand.seed(seed);
    for (unsigned i = 0; i < n; ++i) {
        a[i] = (int32_t) client.rand();
    }
    for (unsigned i = 0; i < n; ++i) {
        std::swap(a[i], a[client.rand() % n]);
    }

    double tg0 = client.now();
    unsigned g;
    for (g = 0; !client.timeout(1); ++g) {
        client.get_check(0, 1);
    }
    client.wait_all();
    double tg1 = client.now();

    Json result = Json();
    kvtest_set_time(result, "puts", n, tp1 - tp0);
    kvtest_set_time(result, "gets", g, tg1 - tg0);
    kvtest_set_time(result, "ops", n + g, (tp1 - tp0) + (tg1 - tg0));
    client.report(result);
    free(a);
}

template <typename C>
void kvtest_udp1(C &client)
{
    kvtest_udp1_seed(client, kvtest_first_seed + client.id() % 48);
}

// do four million of inserts to distinct keys.
// sometimes overwrites, but only w/ same value.
// different clients might use same key sometimes.
template <typename C>
void kvtest_w1_seed(C &client, int seed)
{
    int n;
    if (client.limit() == ~(uint64_t) 0) {
        n = 4000000;
    } else {
        n = std::min(client.limit(), (uint64_t) INT_MAX);
    }
    client.rand.seed(seed);

    double t0 = now();
    for (int i = 0; i < n; i++) {
        long x = client.rand();
        client.put_key10(x, x + 1);
    }
    client.wait_all();
    double t1 = now();

    Json result = Json().set("total", (long) (n / (t1 - t0)))
        .set("puts", n)
        .set("puts_per_sec", n / (t1 - t0));
    client.report(result);
}

// do four million gets.
// in a random order.
// if we get in the same order that w1 put, performance is
// about 15% better for b-tree.
template <typename C>
void kvtest_r1_seed(C &client, int seed)
{
    int n;
    if (client.limit() == ~(uint64_t) 0) {
        n = 4000000;
    } else {
        n = std::min(client.limit(), (uint64_t) INT_MAX);
    }
    long *a = (long *) malloc(sizeof(long) * n);
    always_assert(a);

    client.rand.seed(seed);
    for (int i = 0; i < n; i++) {
        a[i] = client.rand();
    }
    for (int i = 0; i < n; i++) {
        int i1 = client.rand() % n;
        long tmp = a[i];
        a[i] = a[i1];
        a[i1] = tmp;
    }

    double t0 = now();
    for (int i = 0; i < n; i++) {
        client.get_check_key10(a[i], a[i] + 1);
    }
    client.wait_all();
    double t1 = now();

    Json result = Json().set("total", (long) (n / (t1 - t0)))
        .set("gets", n)
        .set("gets_per_sec", n / (t1 - t0));
    client.report(result);
}

// do four million of inserts to distinct keys.
// sometimes overwrites, but only w/ same value.
// different clients might use same key sometimes.
template <typename C>
void kvtest_wcol1at(C &client, int col, int seed, long maxkeys)
{
    int n;
    if (client.limit() == ~(uint64_t) 0) {
        n = 4000000;
    } else {
        n = std::min(client.limit(), (uint64_t) INT_MAX);
    }
    client.rand.seed(seed);

    double t0 = now();
    for (int i = 0; i < n; i++) {
        long x = client.rand() % maxkeys;
        client.put_col_key10(x, col, x + 1);
    }
    client.wait_all();
    double t1 = now();

    Json result = Json().set("total", (long) (n / (t1 - t0)))
        .set("puts", n)
        .set("puts_per_sec", n / (t1 - t0));
    client.report(result);
}

// do four million gets.
// in a random order.
// if we get in the same order that w1 put, performance is
// about 15% better for b-tree.
template <typename C>
void kvtest_rcol1at(C &client, int col, int seed, long maxkeys)
{
    int n;
    if (client.limit() == ~(uint64_t) 0) {
        n = 4000000;
    } else {
        n = std::min(client.limit(), (uint64_t) INT_MAX);
    }
    long *a = (long *) malloc(sizeof(long) * n);
    always_assert(a);

    client.rand.seed(seed);
    for (int i = 0; i < n; i++) {
        a[i] = client.rand() % maxkeys;
    }
    for (int i = 0; i < n && 0; i++) {
        int i1 = client.rand() % n;
        long tmp = a[i];
        a[i] = a[i1];
        a[i1] = tmp;
    }

    double t0 = now();
    for (int i = 0; i < n; i++) {
        client.get_col_check_key10(a[i], col, a[i] + 1);
    }
    client.wait_all();
    double t1 = now();

    Json result = Json().set("total", (long) (n / (t1 - t0)))
        .set("gets", n)
        .set("gets_per_sec", n / (t1 - t0));
    client.report(result);
}

// test scans with parallel inserts
template <typename C>
void kvtest_scan1(C &client, double writer_quiet)
{
    int n, wq65536 = int(writer_quiet * 65536);
    if (client.limit() == ~(uint64_t) 0) {
        n = 10000;
    } else {
        n = std::min(client.limit(), (uint64_t) 97655);
    }
    Json result;

    if (client.id() % 24 == 0) {
        for (int i = 0; i < n; ++i) {
            client.put_key8(i * 1024, i);
        }
        client.wait_all();

        int pos = 0, mypos = 0, scansteps = 0;
        quick_istr key;
        std::vector<Str> keys, values;
        Json errj;
        while (!client.timeout(0) && errj.size() < 1000) {
            key.set(pos, 8);
            client.scan_sync(key.string(), 100, keys, values);
            if (keys.size() == 0) {
                if (mypos < n * 1024) {
                    errj.push_back("missing " + String(mypos) + " through " + String((n - 1) * 1024));
                }
                pos = mypos = 0;
            } else {
                for (size_t i = 0; i < keys.size(); ++i) {
                    int val = keys[i].to_i();
                    if (val < 0) {
                        errj.push_back("unexpected key " + String(keys[i].s, keys[i].len));
                        continue;
                    }
                    if (val < pos) {
                        errj.push_back("got " + String(keys[i].s, keys[i].len) + ", expected " + String(pos) + " or later");
                    }
                    pos = val + 1;
                    while (val > mypos) {
                        errj.push_back("got " + String(keys[i].s, keys[i].len) + ", missing " + String(mypos) + " @" + String(scansteps) + "+" + String(i));
                        mypos += 1024;
                    }
                    if (val == mypos) {
                        mypos = val + 1024;
                        ++scansteps;
                    }
                }
            }
            client.rcu_quiesce();
        }
        if (errj.size() >= 1000) {
            errj.push_back("too many errors, giving up");
        }
        result.set("ok", errj.empty()).set("scansteps", scansteps);
        if (errj) {
            result.set("errors", errj);
        }

    } else {
        int delta = 1 + (client.id() % 30) * 32, rounds = 0;
        while (!client.timeout(0)) {
            int first = (client.rand() % n) * 1024 + delta;
            int rand = client.rand() % 65536;
            if (rand < wq65536) {
                for (int d = 0; d < 31; ++d) {
                    relax_fence();
                }
            } else if (rounds > 100 && (rand % 2) == 1) {
                for (int d = 0; d < 31; ++d) {
                    client.remove_key8(d + first);
                }
            } else {
                for (int d = 0; d < 31; ++d) {
                    client.put_key8(d + first, d + first);
                }
            }
            ++rounds;
            client.rcu_quiesce();
        }
    }

    client.report(result);
}

// test reverse scans with parallel inserts
template <typename C>
void kvtest_rscan1(C &client, double writer_quiet)
{
    int n, wq65536 = int(writer_quiet * 65536);
    if (client.limit() == ~(uint64_t) 0) {
        n = 10000;
    } else {
        n = std::min(client.limit(), (uint64_t) 97655);
    }
    Json result;

    if (client.id() % 24 == 0) {
        for (int i = 1; i <= n; ++i) {
            client.put_key8(i * 1024, i);
        }
        client.wait_all();

        int pos = (n + 1) * 1024, mypos = n * 1024, scansteps = 0;
        quick_istr key;
        std::vector<Str> keys, values;
        Json errj;
        while (!client.timeout(0) && errj.size() < 1000) {
            key.set(pos, 8);
            client.rscan_sync(key.string(), 100, keys, values);
            if (keys.size() == 0) {
                if (mypos > 0) {
                    errj.push_back("missing 1024 through " + String(mypos) + " @" + String(scansteps));
                }
                pos = (n + 1) * 1024, mypos = n * 1024;
            } else {
                for (size_t i = 0; i < keys.size(); ++i) {
                    int val = keys[i].to_i();
                    if (val < 0) {
                        errj.push_back("unexpected key " + String(keys[i].s, keys[i].len));
                        continue;
                    }
                    if (val > pos) {
                        errj.push_back("got " + String(keys[i].s, keys[i].len) + ", expected " + String(pos) + " or less");
                    }
                    pos = val - 1;
                    while (val < mypos) {
                        String last;
                        if (i) {
                            last = String(keys[i-1].s, keys[i-1].len);
                        } else {
                            last = String(key.string().s, key.string().len);
                        }
                        errj.push_back("got " + String(keys[i].s, keys[i].len) + ", missing " + String(mypos) + " @" + String(scansteps) + "+" + String(i) + ", last " + last);
                        mypos -= 1024;
                    }
                    if (val == mypos) {
                        mypos = val - 1024;
                        ++scansteps;
                    }
                }
            }
            client.rcu_quiesce();
        }
        if (errj.size() >= 1000) {
            errj.push_back("too many errors, giving up");
        }
        result.set("ok", errj.empty()).set("scansteps", scansteps);
        if (errj) {
            result.set("errors", errj);
        }

    } else {
        int delta = 1 + (client.id() % 30) * 32, rounds = 0;
        while (!client.timeout(0)) {
            int first = (client.rand() % n + 1) * 1024 + delta;
            int rand = client.rand() % 65536;
            if (rand < wq65536) {
                for (int d = 0; d < 31; ++d) {
                    relax_fence();
                }
            } else if (rounds > 100 && (rand % 2) == 1) {
                for (int d = 0; d < 31; ++d) {
                    client.remove_key8(d + first);
                }
            } else {
                for (int d = 0; d < 31; ++d) {
                    client.put_key8(d + first, d + first);
                }
            }
            ++rounds;
            client.rcu_quiesce();
        }
    }

    client.report(result);
}

// test concurrent splits with removes in lower layers
template <typename C>
void kvtest_splitremove1(C &client)
{
    // XXX these parameters depend on masstree constants...
    int leaf_width = 15, internode_width = 15;
    int num_keys = leaf_width * (internode_width + 1) + 1;
    int trigger_key = num_keys - 15;
    int rounds = 0;
    Json result, errj;

    if (client.id() == 0) {
        while (1) {
            for (int i = 0; i < num_keys; ++i) {
                client.put_key16(i + 100, i + 101);
            }
            client.rcu_quiesce();
            for (int i = trigger_key + 1; i < num_keys + 10; ++i) {
                client.remove_key16(i + 100);
            }
            client.rcu_quiesce();
            for (int i = 0; i < leaf_width * internode_width; ++i) {
                client.put_key16(i, i + 1);
            }

            client.put(client.nthreads(), client.nthreads() + 1);
            for (int i = 1; i < client.nthreads(); ++i) {
                client.put(i, i + 1);
            }
            for (int i = 1; i < client.nthreads(); ++i) {
                while (!client.timeout(0) && client.get_sync(i)) {
                    /* do nothing */
                }
            }
            client.remove_key16(trigger_key);
            client.remove(client.nthreads());
            if (client.timeout(0)) {
                break;
            }

            for (int i = 0; i < num_keys; ++i) {
                client.remove_key16(i);
                client.remove_key16(i + 100);
            }
            for (int i = 0; i < 10; ++i) {
                client.rcu_quiesce();
            }
            ++rounds;
        }

    } else {
        quick_istr me(client.id()), trigger(trigger_key, 16);
        while (1) {
            while (!client.timeout(0) && !client.get_sync_key16(trigger_key)) {
                client.rcu_quiesce();
            }
            if (client.timeout(0)) {
                break;
            }

            for (int i = 0; !client.get_sync(me.string()); ++i) {
                if (!client.get_sync(trigger.string()) && !client.timeout(0)) {
                    if (errj.size() == 100) {
                        errj.push_back("more errors");
                    } else if (errj.size() < 100) {
                        errj.push_back("key " + String(trigger.string()) + " missing after " + String(rounds) + " rounds, counter " + String(i));
                    }
                    break;
                }
                client.rcu_quiesce();
            }

            while (!client.timeout(0) && !client.get_sync(me.string())) {
                client.rcu_quiesce();
            }
            client.remove(me.string());
            while (!client.timeout(0) && client.get_sync(client.nthreads())) {
                client.rcu_quiesce();
            }
            if (client.timeout(0)) {
                break;
            }

            for (int i = 0; i < 10; ++i) {
                client.rcu_quiesce();
            }
            ++rounds;
        }
    }

    result.set("ok", errj.empty()).set("rounds", rounds);
    if (errj) {
        result.set("errors", errj);
    }
    client.report(result);
}

template <typename C>
void kvtest_url_seed(C &client)
{
    if (!client.param("file").is_s()) {
        client.report(Json::object("ok", false, "error", "need 'file=URLFILE' parameter"));
        return;
    }

    std::ifstream infile_url_init(client.param("file").to_s());
    std::ifstream infile_url_del_get(client.param("file").to_s());
    std::string ops;
    std::string url;
    unsigned count_i = 0;
    unsigned count_d = 0;
    unsigned count_g = 0;

    double t0 = client.now();
    while (count_i < client.limit() && infile_url_init.good()) {
        //do the following alternately:
        //insert 10 urls, then delete 5 inserted urls
        for (int i = 0; i != 10 && infile_url_init >> ops >> url; ++i, ++count_i) {
            client.put(url, 2014);
        }
        for (int i = 0; i != 5 && infile_url_del_get >> ops >> url; ++i, ++count_d) {
            client.remove(url);
        }
    }
    client.wait_all();
    client.puts_done();
    double t1 = client.now();
    infile_url_init.close();
    client.notice("\ninsert done\n");

    //query all the inserted urls
    double t2 = client.now();
    while (count_d + count_g != count_i && infile_url_del_get >> ops >> url) {
        client.get_check(Str(url), 2014);
        ++count_g;
    }
    client.wait_all();
    double t3 = client.now();

    // client.notice("Total pool memory: %d\n", client.ti_->poolmem);
    // client.notice("Total general memory: %d\n", client.ti_->genmem);
    // client.notice("Total MEMORY: %d\n", client.ti_->poolmem + client.ti_->genmem);

    Json result = Json::object("puts", count_i, "removes", count_d);
    kvtest_set_time(result, "gets", count_g, t3 - t2);
    kvtest_set_time(result, "ops", count_i + count_d, t1 - t0);
    client.report(result);
}

template <typename C>
void kvtest_url(C &client)
{
  kvtest_url_seed(client);
}

#endif

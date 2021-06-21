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
// -*- mode: c++ -*-
// mttest: key/value tester
//

#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <limits.h>
#if HAVE_NUMA_H
#include <numa.h>
#endif
#if HAVE_SYS_EPOLL_H
#include <sys/epoll.h>
#endif
#if HAVE_EXECINFO_H
#include <execinfo.h>
#endif
#if __linux__
#include <asm-generic/mman.h>
#endif
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <signal.h>
#include <errno.h>
#ifdef __linux__
#include <malloc.h>
#endif
#include "nodeversion.hh"
#include "kvstats.hh"
#include "query_masstree.hh"
#include "masstree_tcursor.hh"
#include "masstree_insert.hh"
#include "masstree_remove.hh"
#include "masstree_scan.hh"
#include "timestamp.hh"
#include "json.hh"
#include "kvtest.hh"
#include "kvrandom.hh"
#include "kvrow.hh"
#include "kvio.hh"
#include "clp.h"
#include <algorithm>
#include <numeric>

static std::vector<int> cores;
volatile bool timeout[2] = {false, false};
double duration[2] = {10, 0};
int kvtest_first_seed = 31949;
uint64_t test_limit = ~uint64_t(0);
static Json test_param;

bool quiet = false;
bool print_table = false;
static const char *gid = NULL;

// all default to the number of cores
static int udpthreads = 0;
static int tcpthreads = 0;

static bool tree_stats = false;
static bool json_stats = false;
static String gnuplot_yrange;
static bool pinthreads = false;
static nodeversion32 global_epoch_lock(false);
volatile mrcu_epoch_type globalepoch = 1;     // global epoch, updated by main thread regularly
volatile mrcu_epoch_type active_epoch = 1;
kvepoch_t global_log_epoch = 0;
static int port = 2117;
static int rscale_ncores = 0;

#if MEMSTATS && HAVE_NUMA_H && HAVE_LIBNUMA
struct mttest_numainfo {
    long long free;
    long long size;
};
std::vector<mttest_numainfo> numa;
#endif

volatile bool recovering = false; // so don't add log entries, and free old value immediately
kvtimestamp_t initial_timestamp;

static const char *threadcounter_names[(int) tc_max];

/* running local tests */
void test_timeout(int) {
    size_t n;
    for (n = 0; n < arraysize(timeout) && timeout[n]; ++n)
        /* do nothing */;
    if (n < arraysize(timeout)) {
        timeout[n] = true;
        if (n + 1 < arraysize(timeout) && duration[n + 1])
            xalarm(duration[n + 1]);
    }
}

void set_global_epoch(mrcu_epoch_type e) {
    global_epoch_lock.lock();
    if (mrcu_signed_epoch_type(e - globalepoch) > 0) {
        globalepoch = e;
        active_epoch = threadinfo::min_active_epoch();
    }
    global_epoch_lock.unlock();
}

template <typename T>
struct kvtest_client {
    using table_type = T;

    kvtest_client()
        : limit_(test_limit), ncores_(udpthreads), kvo_() {
    }
    ~kvtest_client() {
        if (kvo_)
            free_kvout(kvo_);
    }

    int nthreads() const {
        return udpthreads;
    }
    int id() const {
        return ti_->index();
    }
    void set_table(T* table, threadinfo *ti) {
        table_ = table;
        ti_ = ti;
    }
    void reset(const String &test, int trial) {
        report_ = Json().set("table", T().name())
            .set("test", test).set("trial", trial)
            .set("thread", ti_->index());
    }

    bool timeout(int which) const {
        return ::timeout[which];
    }
    uint64_t limit() const {
        return limit_;
    }
    bool has_param(const String& name) const {
        return test_param.count(name);
    }
    Json param(const String& name, Json default_value = Json()) const {
        return test_param.count(name) ? test_param.at(name) : default_value;
    }

    int ncores() const {
        return ncores_;
    }
    double now() const {
        return ::now();
    }
    int ruscale_partsz() const {
        return (140 * 1000000) / 16;
    }
    int ruscale_init_part_no() const {
        return ti_->index();
    }
    long nseqkeys() const {
        return 16 * ruscale_partsz();
    }

    void get(long ikey);
    bool get_sync(Str key);
    bool get_sync(Str key, Str& value);
    bool get_sync(long ikey) {
        quick_istr key(ikey);
        return get_sync(key.string());
    }
    bool get_sync_key16(long ikey) {
        quick_istr key(ikey, 16);
        return get_sync(key.string());
    }
    void get_check(Str key, Str expected);
    void get_check(const char *key, const char *expected) {
        get_check(Str(key), Str(expected));
    }
    void get_check(long ikey, long iexpected) {
        quick_istr key(ikey), expected(iexpected);
        get_check(key.string(), expected.string());
    }
    void get_check(Str key, long iexpected) {
        quick_istr expected(iexpected);
        get_check(key, expected.string());
    }
    void get_check_key8(long ikey, long iexpected) {
        quick_istr key(ikey, 8), expected(iexpected);
        get_check(key.string(), expected.string());
    }
    void get_col_check(Str key, int col, Str value);
    void get_col_check(long ikey, int col, long ivalue) {
        quick_istr key(ikey), value(ivalue);
        get_col_check(key.string(), col, value.string());
    }
    void get_col_check_key10(long ikey, int col, long ivalue) {
        quick_istr key(ikey, 10), value(ivalue);
        get_col_check(key.string(), col, value.string());
    }
    void get_check_absent(Str key);
    //void many_get_check(int nk, long ikey[], long iexpected[]);

    void scan_sync(Str firstkey, int n,
                   std::vector<Str>& keys, std::vector<Str>& values);
    void rscan_sync(Str firstkey, int n,
                    std::vector<Str>& keys, std::vector<Str>& values);
    void scan_versions_sync(Str firstkey, int n,
                            std::vector<Str>& keys, std::vector<Str>& values);
    const std::vector<uint64_t>& scan_versions() const {
        return scan_versions_;
    }

    void put(Str key, Str value);
    void put(const char *key, const char *value) {
        put(Str(key), Str(value));
    }
    void put(long ikey, long ivalue) {
        quick_istr key(ikey), value(ivalue);
        put(key.string(), value.string());
    }
    void put(Str key, long ivalue) {
        quick_istr value(ivalue);
        put(key, value.string());
    }
    void put_key8(long ikey, long ivalue) {
        quick_istr key(ikey, 8), value(ivalue);
        put(key.string(), value.string());
    }
    void put_key16(long ikey, long ivalue) {
        quick_istr key(ikey, 16), value(ivalue);
        put(key.string(), value.string());
    }
    void put_col(Str key, int col, Str value);
    void put_col(long ikey, int col, long ivalue) {
        quick_istr key(ikey), value(ivalue);
        put_col(key.string(), col, value.string());
    }
    void put_col_key10(long ikey, int col, long ivalue) {
        quick_istr key(ikey, 10), value(ivalue);
        put_col(key.string(), col, value.string());
    }
    void insert_check(Str key, Str value);

    void remove(Str key);
    void remove(long ikey) {
        quick_istr key(ikey);
        remove(key.string());
    }
    void remove_key8(long ikey) {
        quick_istr key(ikey, 8);
        remove(key.string());
    }
    void remove_key16(long ikey) {
        quick_istr key(ikey, 16);
        remove(key.string());
    }
    bool remove_sync(Str key);
    bool remove_sync(long ikey) {
        quick_istr key(ikey);
        return remove_sync(key.string());
    }
    void remove_check(Str key);

    void print() {
        table_->print(stderr);
    }
    void puts_done() {
    }
    void wait_all() {
    }
    void rcu_quiesce() {
        mrcu_epoch_type e = timestamp() >> 16;
        if (e != globalepoch)
            set_global_epoch(e);
        ti_->rcu_quiesce();
    }
    String make_message(lcdf::StringAccum &sa) const;
    void notice(const char *fmt, ...);
    void fail(const char *fmt, ...);
    const Json& report(const Json& x) {
        return report_.merge(x);
    }
    void finish() {
        Json counters;
        for (int i = 0; i < tc_max; ++i) {
            if (uint64_t c = ti_->counter(threadcounter(i)))
                counters.set(threadcounter_names[i], c);
        }
        if (counters) {
            report_.set("counters", counters);
        }
        if (!quiet) {
            fprintf(stderr, "%d: %s\n", ti_->index(), report_.unparse().c_str());
        }
    }

    T *table_;
    threadinfo *ti_;
    query<row_type> q_[1];
    kvrandom_lcg_nr rand;
    uint64_t limit_;
    Json report_;
    Json req_;
    std::vector<uint64_t> scan_versions_;
    int ncores_;
    kvout *kvo_;

  private:
    void output_scan(const Json& req, std::vector<Str>& keys, std::vector<Str>& values) const;
};

static volatile int kvtest_printing;

template <typename T> inline void kvtest_print(const T &table, FILE* f, threadinfo *ti) {
    // only print out the tree from the first failure
    while (!bool_cmpxchg((int *) &kvtest_printing, 0, ti->index() + 1)) {
    }
    table.print(f);
}

template <typename T> inline void kvtest_json_stats(T& table, Json& j, threadinfo& ti) {
    table.json_stats(j, ti);
}

template <typename T>
void kvtest_client<T>::get(long ikey) {
    quick_istr key(ikey);
    Str val;
    (void) q_[0].run_get1(table_->table(), key.string(), 0, val, *ti_);
}

template <typename T>
bool kvtest_client<T>::get_sync(Str key) {
    Str val;
    return q_[0].run_get1(table_->table(), key, 0, val, *ti_);
}

template <typename T>
bool kvtest_client<T>::get_sync(Str key, Str& value) {
    return q_[0].run_get1(table_->table(), key, 0, value, *ti_);
}

template <typename T>
void kvtest_client<T>::get_check(Str key, Str expected) {
    Str val;
    if (unlikely(!q_[0].run_get1(table_->table(), key, 0, val, *ti_))) {
        fail("get(%s) failed (expected %s)\n", String(key).printable().c_str(),
             String(expected).printable().c_str());
    } else if (unlikely(expected != val)) {
        fail("get(%s) returned unexpected value %s (expected %s)\n",
             String(key).printable().c_str(),
             String(val).substr(0, 40).printable().c_str(),
             String(expected).substr(0, 40).printable().c_str());
    }
}

template <typename T>
void kvtest_client<T>::get_col_check(Str key, int col,
                                     Str expected) {
    Str val;
    if (unlikely(!q_[0].run_get1(table_->table(), key, col, val, *ti_))) {
        fail("get.%d(%.*s) failed (expected %.*s)\n",
             col, key.len, key.s, expected.len, expected.s);
    } else if (unlikely(expected != val)) {
        fail("get.%d(%.*s) returned unexpected value %.*s (expected %.*s)\n",
             col, key.len, key.s, std::min(val.len, 40), val.s,
             expected.len, expected.s);
    }
}

template <typename T>
void kvtest_client<T>::get_check_absent(Str key) {
    Str val;
    if (unlikely(q_[0].run_get1(table_->table(), key, 0, val, *ti_))) {
        fail("get(%s) failed (expected absent key)\n", String(key).printable().c_str());
    }
}

/*template <typename T>
void kvtest_client<T>::many_get_check(int nk, long ikey[], long iexpected[]) {
    std::vector<quick_istr> ka(2*nk, quick_istr());
    for(int i = 0; i < nk; i++){
      ka[i].set(ikey[i]);
      ka[i+nk].set(iexpected[i]);
      q_[i].begin_get1(ka[i].string());
    }
    table_->many_get(q_, nk, *ti_);
    for(int i = 0; i < nk; i++){
      Str val = q_[i].get1_value();
      if (ka[i+nk] != val){
        printf("get(%ld) returned unexpected value %.*s (expected %ld)\n",
             ikey[i], std::min(val.len, 40), val.s, iexpected[i]);
        exit(1);
      }
    }
}*/

template <typename T>
void kvtest_client<T>::scan_sync(Str firstkey, int n,
                                 std::vector<Str>& keys,
                                 std::vector<Str>& values) {
    req_ = Json::array(0, 0, firstkey, n);
    q_[0].run_scan(table_->table(), req_, *ti_);
    output_scan(req_, keys, values);
}

template <typename T>
void kvtest_client<T>::rscan_sync(Str firstkey, int n,
                                  std::vector<Str>& keys,
                                  std::vector<Str>& values) {
    req_ = Json::array(0, 0, firstkey, n);
    q_[0].run_rscan(table_->table(), req_, *ti_);
    output_scan(req_, keys, values);
}

template <typename T>
void kvtest_client<T>::scan_versions_sync(Str firstkey, int n,
                                          std::vector<Str>& keys,
                                          std::vector<Str>& values) {
    req_ = Json::array(0, 0, firstkey, n);
    scan_versions_.clear();
    q_[0].run_scan_versions(table_->table(), req_, scan_versions_, *ti_);
    output_scan(req_, keys, values);
}

template <typename T>
void kvtest_client<T>::output_scan(const Json& req, std::vector<Str>& keys,
                                   std::vector<Str>& values) const {
    keys.clear();
    values.clear();
    for (int i = 2; i != req.size(); i += 2) {
        keys.push_back(req[i].as_s());
        values.push_back(req[i + 1].as_s());
    }
}

template <typename T>
void kvtest_client<T>::put(Str key, Str value) {
    q_[0].run_replace(table_->table(), key, value, *ti_);
}

template <typename T>
void kvtest_client<T>::insert_check(Str key, Str value) {
    if (unlikely(q_[0].run_replace(table_->table(), key, value, *ti_) != Inserted)) {
        fail("insert(%s) did not insert\n", String(key).printable().c_str());
    }
}

template <typename T>
void kvtest_client<T>::put_col(Str key, int col, Str value) {
#if !MASSTREE_ROW_TYPE_STR
    if (!kvo_) {
        kvo_ = new_kvout(-1, 2048);
    }
    Json x[2] = {Json(col), Json(String::make_stable(value))};
    q_[0].run_put(table_->table(), key, &x[0], &x[2], *ti_);
#else
    (void) key, (void) col, (void) value;
    assert(0);
#endif
}

template <typename T> inline bool kvtest_remove(kvtest_client<T> &client, Str key) {
    return client.q_[0].run_remove(client.table_->table(), key, *client.ti_);
}

template <typename T>
void kvtest_client<T>::remove(Str key) {
    (void) kvtest_remove(*this, key);
}

template <typename T>
bool kvtest_client<T>::remove_sync(Str key) {
    return kvtest_remove(*this, key);
}

template <typename T>
void kvtest_client<T>::remove_check(Str key) {
    if (unlikely(!kvtest_remove(*this, key))) {
        fail("remove(%s) did not remove\n", String(key).printable().c_str());
    }
}

template <typename T>
String kvtest_client<T>::make_message(lcdf::StringAccum &sa) const {
    const char *begin = sa.begin();
    while (begin != sa.end() && isspace((unsigned char) *begin))
        ++begin;
    String s = String(begin, sa.end());
    if (!s.empty() && s.back() != '\n')
        s += '\n';
    return s;
}

template <typename T>
void kvtest_client<T>::notice(const char *fmt, ...) {
    va_list val;
    va_start(val, fmt);
    String m = make_message(lcdf::StringAccum().vsnprintf(500, fmt, val));
    va_end(val);
    if (m && !quiet)
        fprintf(stderr, "%d: %s", ti_->index(), m.c_str());
}

template <typename T>
void kvtest_client<T>::fail(const char *fmt, ...) {
    static nodeversion32 failing_lock(false);
    static nodeversion32 fail_message_lock(false);
    static String fail_message;

    va_list val;
    va_start(val, fmt);
    String m = make_message(lcdf::StringAccum().vsnprintf(500, fmt, val));
    va_end(val);
    if (!m)
        m = "unknown failure";

    fail_message_lock.lock();
    if (fail_message != m) {
        fail_message = m;
        fprintf(stderr, "%d: %s", ti_->index(), m.c_str());
    }
    fail_message_lock.unlock();

    failing_lock.lock();
    fprintf(stdout, "%d: %s", ti_->index(), m.c_str());
    kvtest_print(*table_, stdout, ti_);

    always_assert(0);
}


static const char *current_test_name;
static int current_trial;
static FILE *test_output_file;
static pthread_mutex_t subtest_mutex;
static pthread_cond_t subtest_cond;

#define TESTRUNNER_CLIENT_TYPE kvtest_client<Masstree::default_table>&
#include "testrunner.hh"

MAKE_TESTRUNNER(rw1, kvtest_rw1(client));
// MAKE_TESTRUNNER(palma, kvtest_palma(client));
// MAKE_TESTRUNNER(palmb, kvtest_palmb(client));
MAKE_TESTRUNNER(rw1fixed, kvtest_rw1fixed(client));
MAKE_TESTRUNNER(rw1long, kvtest_rw1long(client));
MAKE_TESTRUNNER(rw1puts, kvtest_rw1puts(client));
MAKE_TESTRUNNER(rw2, kvtest_rw2(client));
MAKE_TESTRUNNER(rw2fixed, kvtest_rw2fixed(client));
MAKE_TESTRUNNER(rw2g90, kvtest_rw2g90(client));
MAKE_TESTRUNNER(rw2fixedg90, kvtest_rw2fixedg90(client));
MAKE_TESTRUNNER(rw2g98, kvtest_rw2g98(client));
MAKE_TESTRUNNER(rw2fixedg98, kvtest_rw2fixedg98(client));
MAKE_TESTRUNNER(rw3, kvtest_rw3(client));
MAKE_TESTRUNNER(rw4, kvtest_rw4(client));
MAKE_TESTRUNNER(rw4fixed, kvtest_rw4fixed(client));
MAKE_TESTRUNNER(wd1, kvtest_wd1(10000000, 1, client));
MAKE_TESTRUNNER(wd1m1, kvtest_wd1(100000000, 1, client));
MAKE_TESTRUNNER(wd1m2, kvtest_wd1(1000000000, 4, client));
MAKE_TESTRUNNER(wd3, kvtest_wd3(client, 70 * client.nthreads()));
MAKE_TESTRUNNER(same, kvtest_same(client));
MAKE_TESTRUNNER(rwsmall24, kvtest_rwsmall24(client));
MAKE_TESTRUNNER(rwsep24, kvtest_rwsep24(client));
MAKE_TESTRUNNER(wscale, kvtest_wscale(client));
MAKE_TESTRUNNER(ruscale_init, kvtest_ruscale_init(client));
MAKE_TESTRUNNER(rscale, if (client.ti_->index() < ::rscale_ncores) kvtest_rscale(client));
MAKE_TESTRUNNER(uscale, kvtest_uscale(client));
MAKE_TESTRUNNER(bdb, kvtest_bdb(client));
MAKE_TESTRUNNER(wcol1, kvtest_wcol1at(client, client.id() % 24, kvtest_first_seed + client.id() % 48, 5000000));
MAKE_TESTRUNNER(rcol1, kvtest_rcol1at(client, client.id() % 24, kvtest_first_seed + client.id() % 48, 5000000));
MAKE_TESTRUNNER(wcol1o1, kvtest_wcol1at(client, (client.id() + 1) % 24, kvtest_first_seed + client.id() % 48, 5000000));
MAKE_TESTRUNNER(rcol1o1, kvtest_rcol1at(client, (client.id() + 1) % 24, kvtest_first_seed + client.id() % 48, 5000000));
MAKE_TESTRUNNER(wcol1o2, kvtest_wcol1at(client, (client.id() + 2) % 24, kvtest_first_seed + client.id() % 48, 5000000));
MAKE_TESTRUNNER(rcol1o2, kvtest_rcol1at(client, (client.id() + 2) % 24, kvtest_first_seed + client.id() % 48, 5000000));
MAKE_TESTRUNNER(scan1, kvtest_scan1(client, 0));
MAKE_TESTRUNNER(scan1q80, kvtest_scan1(client, 0.8));
MAKE_TESTRUNNER(rscan1, kvtest_rscan1(client, 0));
MAKE_TESTRUNNER(rscan1q80, kvtest_rscan1(client, 0.8));
MAKE_TESTRUNNER(splitremove1, kvtest_splitremove1(client));
MAKE_TESTRUNNER(url, kvtest_url(client));
MAKE_TESTRUNNER(conflictscan1, kvtest_conflictscan1(client));


enum {
    test_thread_initialize = 1,
    test_thread_destroy = 2,
    test_thread_stats = 3
};

template <typename T>
struct test_thread {
    test_thread(threadinfo* ti) {
        client_.set_table(table_, ti);
        client_.ti_->rcu_start();
    }
    ~test_thread() {
        client_.ti_->rcu_stop();
    }
    static void setup(threadinfo* ti, int action) {
        if (action == test_thread_initialize) {
            assert(!table_);
            table_ = new T;
            table_->initialize(*ti);
        } else if (action == test_thread_destroy) {
            assert(table_);
            delete table_;
            table_ = 0;
        } else if (action == test_thread_stats) {
            assert(table_);
            table_->stats(test_output_file);
        }
    }
    static void* go(void* x) {
        threadinfo* ti = reinterpret_cast<threadinfo*>(x);
        ti->pthread() = pthread_self();
        assert(table_);
#if __linux__
        if (pinthreads) {
            cpu_set_t cs;
            CPU_ZERO(&cs);
            CPU_SET(cores[ti->index()], &cs);
            int r = sched_setaffinity(0, sizeof(cs), &cs);
            always_assert(r == 0);
        }
#else
        always_assert(!pinthreads && "pinthreads not supported\n");
#endif

        test_thread<T> tt(ti);
        if (fetch_and_add(&active_threads_, 1) == 0)
            tt.ready_timeouts();
        String test = ::current_test_name;
        int subtestno = 0;
        for (int pos = 0; pos < test.length(); ) {
            int comma = test.find_left(',', pos);
            comma = (comma < 0 ? test.length() : comma);
            String subtest = test.substr(pos, comma - pos), tname;
            testrunner* tr = testrunner::find(subtest);
            tname = (subtest == test ? subtest : test + String("@") + String(subtestno));
            tt.client_.reset(tname, ::current_trial);
            if (tr)
                tr->run(tt.client_);
            else
                tt.client_.fail("unknown test %s", subtest.c_str());
            if (comma == test.length())
                break;
            pthread_mutex_lock(&subtest_mutex);
            if (fetch_and_add(&active_threads_, -1) == 1) {
                pthread_cond_broadcast(&subtest_cond);
                tt.ready_timeouts();
            } else
                pthread_cond_wait(&subtest_cond, &subtest_mutex);
            fprintf(test_output_file, "%s\n", tt.client_.report_.unparse().c_str());
            pthread_mutex_unlock(&subtest_mutex);
            fetch_and_add(&active_threads_, 1);
            pos = comma + 1;
            ++subtestno;
        }
        int at = fetch_and_add(&active_threads_, -1);
        if (at == 1 && print_table) {
            kvtest_print(*table_, stdout, tt.client_.ti_);
        }
        if (at == 1 && json_stats) {
            Json j;
            kvtest_json_stats(*table_, j, *tt.client_.ti_);
            if (j) {
                fprintf(stderr, "%s\n", j.unparse(Json::indent_depth(1).tab_width(2).newline_terminator(true)).c_str());
                tt.client_.report_.merge(j);
            }
        }
        fprintf(test_output_file, "%s\n", tt.client_.report_.unparse().c_str());
        return 0;
    }
    void ready_timeouts() {
        for (size_t i = 0; i < arraysize(timeout); ++i) {
            timeout[i] = false;
        }
        if (duration[0]) {
            xalarm(duration[0]);
        }
    }
    static T* table_;
    static unsigned active_threads_;
    kvtest_client<T> client_;
};
template <typename T> T* test_thread<T>::table_;
template <typename T> unsigned test_thread<T>::active_threads_;

typedef test_thread<Masstree::default_table> masstree_test_thread;

static struct {
    const char *treetype;
    void* (*go_func)(void*);
    void (*setup_func)(threadinfo*, int);
} test_thread_map[] = {
    { "masstree", masstree_test_thread::go, masstree_test_thread::setup },
    { "mass", masstree_test_thread::go, masstree_test_thread::setup },
    { "mbtree", masstree_test_thread::go, masstree_test_thread::setup },
    { "mb", masstree_test_thread::go, masstree_test_thread::setup },
    { "m", masstree_test_thread::go, masstree_test_thread::setup }
};


void runtest(int nthreads, void* (*func)(void*)) {
    std::vector<threadinfo*> tis;
    for (int i = 0; i < nthreads; ++i)
        tis.push_back(threadinfo::make(threadinfo::TI_PROCESS, i));
    signal(SIGALRM, test_timeout);
    for (int i = 0; i < nthreads; ++i) {
        int r = pthread_create(&tis[i]->pthread(), 0, func, tis[i]);
        always_assert(r == 0);
    }
    for (int i = 0; i < nthreads; ++i)
        pthread_join(tis[i]->pthread(), 0);
}


static const char * const kvstats_name[] = {
    "ops_per_sec", "puts_per_sec", "gets_per_sec", "scans_per_sec"
};

static Json experiment_stats;

void *stat_collector(void *arg) {
    int p = (int) (intptr_t) arg;
    FILE *f = fdopen(p, "r");
    char buf[8192];
    while (fgets(buf, sizeof(buf), f)) {
        Json result = Json::parse(buf);
        if (result && result["table"] && result["test"]) {
            String key = result["test"].to_s() + "/" + result["table"].to_s()
                + "/" + result["trial"].to_s();
            Json &thisex = experiment_stats.get_insert(key);
            thisex[result["thread"].to_i()] = result;
        } else
            fprintf(stderr, "%s\n", buf);
    }
    fclose(f);
    return 0;
}


/* main loop */

enum { clp_val_normalize = Clp_ValFirstUser, clp_val_suffixdouble };
enum { opt_pin = 1, opt_port, opt_duration,
       opt_test, opt_test_name, opt_threads, opt_trials, opt_quiet, opt_print,
       opt_normalize, opt_limit, opt_notebook, opt_compare, opt_no_run,
       opt_gid, opt_tree_stats, opt_rscale_ncores, opt_cores,
       opt_stats, opt_help, opt_yrange };
static const Clp_Option options[] = {
    { "pin", 'p', opt_pin, 0, Clp_Negate },
    { "port", 0, opt_port, Clp_ValInt, 0 },
    { "duration", 'd', opt_duration, Clp_ValDouble, 0 },
    { "limit", 'l', opt_limit, clp_val_suffixdouble, 0 },
    { "normalize", 0, opt_normalize, clp_val_normalize, Clp_Negate },
    { "test", 0, opt_test, Clp_ValString, 0 },
    { "rscale_ncores", 'r', opt_rscale_ncores, Clp_ValInt, 0 },
    { "test-rw1", 0, opt_test_name, 0, 0 },
    { "test-rw2", 0, opt_test_name, 0, 0 },
    { "test-rw3", 0, opt_test_name, 0, 0 },
    { "test-rw4", 0, opt_test_name, 0, 0 },
    { "test-rd1", 0, opt_test_name, 0, 0 },
    { "threads", 'j', opt_threads, Clp_ValInt, 0 },
    { "trials", 'T', opt_trials, Clp_ValInt, 0 },
    { "quiet", 'q', opt_quiet, 0, Clp_Negate },
    { "print", 0, opt_print, 0, Clp_Negate },
    { "notebook", 'b', opt_notebook, Clp_ValString, Clp_Negate },
    { "gid", 'g', opt_gid, Clp_ValString, 0 },
    { "tree-stats", 0, opt_tree_stats, 0, 0 },
    { "stats", 0, opt_stats, 0, 0 },
    { "compare", 'c', opt_compare, Clp_ValString, 0 },
    { "cores", 0, opt_cores, Clp_ValString, 0 },
    { "yrange", 0, opt_yrange, Clp_ValString, 0 },
    { "no-run", 'n', opt_no_run, 0, 0 },
    { "help", 0, opt_help, 0, 0 }
};

static void help() {
    printf("Masstree-beta mttest\n\
Usage: mttest [-jTHREADS] [OPTIONS] [PARAM=VALUE...] TEST...\n\
       mttest -n -c TESTNAME...\n\
\n\
Options:\n\
  -j, --threads=THREADS    Run with THREADS threads (default %d).\n\
  -p, --pin                Pin each thread to its own core.\n\
  -T, --trials=TRIALS      Run each test TRIALS times.\n\
  -q, --quiet              Do not generate verbose and Gnuplot output.\n\
  -l, --limit=LIMIT        Limit relevant tests to LIMIT operations.\n\
  -d, --duration=TIME      Limit relevant tests to TIME seconds.\n\
  -b, --notebook=FILE      Record JSON results in FILE (notebook-mttest.json).\n\
      --no-notebook        Do not record JSON results.\n\
      --print              Print table after test.\n\
\n\
  -n, --no-run             Do not run new tests.\n\
  -c, --compare=EXPERIMENT Generated plot compares to EXPERIMENT.\n\
      --yrange=YRANGE      Set Y range for plot.\n\
\n\
Known TESTs:\n",
           (int) sysconf(_SC_NPROCESSORS_ONLN));
    testrunner_base::print_names(stdout, 5);
    printf("Or say TEST1,TEST2,... to run several tests in sequence\n\
on the same tree.\n");
    exit(0);
}

static void run_one_test(int trial, const char *treetype, const char *test,
                         const int *collectorpipe, int nruns);
enum { normtype_none, normtype_pertest, normtype_firsttest };
static void print_gnuplot(FILE *f, const char * const *types_begin, const char * const *types_end, std::vector<String> &comparisons, int normalizetype);
static void update_labnotebook(String notebook);

#if HAVE_EXECINFO_H
static const int abortable_signals[] = {
    SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE
};

static void abortable_signal_handler(int) {
    // reset signals so if a signal recurs, we exit
    for (const int* it = abortable_signals;
         it != abortable_signals + arraysize(abortable_signals); ++it)
        signal(*it, SIG_DFL);
    // dump backtrace to standard error
    void* return_addrs[50];
    int n = backtrace(return_addrs, arraysize(return_addrs));
    backtrace_symbols_fd(return_addrs, n, STDERR_FILENO);
    // re-abort
    abort();
}
#endif

int
main(int argc, char *argv[])
{
    threadcounter_names[(int) tc_root_retry] = "root_retry";
    threadcounter_names[(int) tc_internode_retry] = "internode_retry";
    threadcounter_names[(int) tc_leaf_retry] = "leaf_retry";
    threadcounter_names[(int) tc_leaf_walk] = "leaf_walk";
    threadcounter_names[(int) tc_stable_internode_insert] = "stable_internode_insert";
    threadcounter_names[(int) tc_stable_internode_split] = "stable_internode_split";
    threadcounter_names[(int) tc_stable_leaf_insert] = "stable_leaf_insert";
    threadcounter_names[(int) tc_stable_leaf_split] = "stable_leaf_split";
    threadcounter_names[(int) tc_internode_lock] = "internode_lock_retry";
    threadcounter_names[(int) tc_leaf_lock] = "leaf_lock_retry";

    int ret, ntrials = 1, normtype = normtype_pertest, firstcore = -1, corestride = 1;
    std::vector<const char *> tests, treetypes;
    std::vector<String> comparisons;
    const char *notebook = "notebook-mttest.json";
    tcpthreads = udpthreads = sysconf(_SC_NPROCESSORS_ONLN);

    Clp_Parser *clp = Clp_NewParser(argc, argv, (int) arraysize(options), options);
    Clp_AddStringListType(clp, clp_val_normalize, 0,
                          "none", (int) normtype_none,
                          "pertest", (int) normtype_pertest,
                          "test", (int) normtype_pertest,
                          "firsttest", (int) normtype_firsttest,
                          (const char *) 0);
    Clp_AddType(clp, clp_val_suffixdouble, Clp_DisallowOptions, clp_parse_suffixdouble, 0);
    int opt;
    while ((opt = Clp_Next(clp)) != Clp_Done) {
        switch (opt) {
        case opt_pin:
            pinthreads = !clp->negated;
            break;
        case opt_threads:
            tcpthreads = udpthreads = clp->val.i;
            break;
        case opt_trials:
            ntrials = clp->val.i;
            break;
        case opt_quiet:
            quiet = !clp->negated;
            break;
        case opt_print:
            print_table = !clp->negated;
            break;
        case opt_rscale_ncores:
            rscale_ncores = clp->val.i;
            break;
        case opt_port:
            port = clp->val.i;
            break;
        case opt_duration:
            duration[0] = clp->val.d;
            break;
        case opt_limit:
            test_limit = uint64_t(clp->val.d);
            break;
        case opt_test:
            tests.push_back(clp->vstr);
            break;
        case opt_test_name:
            tests.push_back(clp->option->long_name + 5);
            break;
        case opt_normalize:
            normtype = clp->negated ? normtype_none : clp->val.i;
            break;
        case opt_gid:
            gid = clp->vstr;
            break;
        case opt_tree_stats:
            tree_stats = true;
            break;
        case opt_stats:
            json_stats = true;
            break;
        case opt_yrange:
            gnuplot_yrange = clp->vstr;
            break;
        case opt_notebook:
            if (clp->negated)
                notebook = 0;
            else if (clp->have_val)
                notebook = clp->vstr;
            else
                notebook = "notebook-mttest.json";
            break;
        case opt_compare:
            comparisons.push_back(clp->vstr);
            break;
        case opt_no_run:
            ntrials = 0;
            break;
      case opt_cores:
          if (firstcore >= 0 || cores.size() > 0) {
              Clp_OptionError(clp, "%<%O%> already given");
              exit(EXIT_FAILURE);
          } else {
              const char *plus = strchr(clp->vstr, '+');
              Json ij = Json::parse(clp->vstr),
                  aj = Json::parse(String("[") + String(clp->vstr) + String("]")),
                  pj1 = Json::parse(plus ? String(clp->vstr, plus) : "x"),
                  pj2 = Json::parse(plus ? String(plus + 1) : "x");
              for (int i = 0; aj && i < aj.size(); ++i)
                  if (!aj[i].is_int() || aj[i].to_i() < 0)
                      aj = Json();
              if (ij && ij.is_int() && ij.to_i() >= 0)
                  firstcore = ij.to_i(), corestride = 1;
              else if (pj1 && pj2 && pj1.is_int() && pj1.to_i() >= 0 && pj2.is_int())
                  firstcore = pj1.to_i(), corestride = pj2.to_i();
              else if (aj) {
                  for (int i = 0; i < aj.size(); ++i)
                      cores.push_back(aj[i].to_i());
              } else {
                  Clp_OptionError(clp, "bad %<%O%>, expected %<CORE1%>, %<CORE1+STRIDE%>, or %<CORE1,CORE2,...%>");
                  exit(EXIT_FAILURE);
              }
          }
          break;
        case opt_help:
            help();
            break;
        case Clp_NotOption:
            // check for parameter setting
            if (const char* eqchr = strchr(clp->vstr, '=')) {
                Json& param = test_param[String(clp->vstr, eqchr)];
                const char* end_vstr = clp->vstr + strlen(clp->vstr);
                if (param.assign_parse(eqchr + 1, end_vstr)) {
                    // OK, param was valid JSON
                } else if (eqchr[1] != 0) {
                    param = String(eqchr + 1, end_vstr);
                } else {
                    param = Json();
                }
            } else {
                // otherwise, tree or test
                bool is_treetype = false;
                for (int i = 0; i < (int) arraysize(test_thread_map) && !is_treetype; ++i) {
                    is_treetype = (strcmp(test_thread_map[i].treetype, clp->vstr) == 0);
                }
                (is_treetype ? treetypes.push_back(clp->vstr) : tests.push_back(clp->vstr));
            }
            break;
        default:
            fprintf(stderr, "Usage: mttest [-jN] TESTS...\n\
Try 'mttest --help' for options.\n");
            exit(EXIT_FAILURE);
        }
    }
    Clp_DeleteParser(clp);
    if (firstcore < 0)
        firstcore = cores.size() ? cores.back() + 1 : 0;
    for (; (int) cores.size() < udpthreads; firstcore += corestride)
        cores.push_back(firstcore);

#if PMC_ENABLED
    always_assert(pinthreads && "Using performance counter requires pinning threads to cores!");
#endif
#if MEMSTATS && HAVE_NUMA_H && HAVE_LIBNUMA
    if (numa_available() != -1)
        for (int i = 0; i <= numa_max_node(); i++) {
            numa.push_back(mttest_numainfo());
            numa.back().size = numa_node_size64(i, &numa.back().free);
        }
#endif
#if HAVE_EXECINFO_H
    for (const int* it = abortable_signals;
         it != abortable_signals + arraysize(abortable_signals); ++it)
        signal(*it, abortable_signal_handler);
#endif

    if (treetypes.empty())
        treetypes.push_back("m");
    if (tests.empty())
        tests.push_back("rw1");

    pthread_mutex_init(&subtest_mutex, 0);
    pthread_cond_init(&subtest_cond, 0);

    // pipe for them to write back to us
    int p[2];
    ret = pipe(p);
    always_assert(ret == 0);
    test_output_file = fdopen(p[1], "w");

    pthread_t collector;
    ret = pthread_create(&collector, 0, stat_collector, (void *) (intptr_t) p[0]);
    always_assert(ret == 0);
    initial_timestamp = timestamp();

    // run tests
    int nruns = ntrials * (int) tests.size() * (int) treetypes.size();
    std::vector<int> runlist(nruns, 0);
    for (int i = 0; i < nruns; ++i)
        runlist[i] = i;

    for (int counter = 0; counter < nruns; ++counter) {
        int x = random() % runlist.size();
        int run = runlist[x];
        runlist[x] = runlist.back();
        runlist.pop_back();

        int trial = run % ntrials;
        run /= ntrials;
        int t = run % tests.size();
        run /= tests.size();
        int tt = run;

        fprintf(stderr, "%d/%u %s/%s%s", counter + 1, (int) (ntrials * tests.size() * treetypes.size()),
                tests[t], treetypes[tt], quiet ? "      " : "\n");

        run_one_test(trial, treetypes[tt], tests[t], p, nruns);
        struct timeval delay;
        delay.tv_sec = 0;
        delay.tv_usec = 250000;
        (void) select(0, 0, 0, 0, &delay);

        if (quiet)
            fprintf(stderr, "\r%60s\r", "");
    }

    fclose(test_output_file);
    pthread_join(collector, 0);

    // update lab notebook
    if (notebook)
        update_labnotebook(notebook);

    // print Gnuplot
    if (ntrials != 0)
        comparisons.insert(comparisons.begin(), "");
    if (!isatty(STDOUT_FILENO) || (ntrials == 0 && comparisons.size()))
        print_gnuplot(stdout, kvstats_name, kvstats_name + arraysize(kvstats_name),
                      comparisons, normtype);

    return 0;
}

static void run_one_test_body(int trial, const char *treetype, const char *test) {
    threadinfo *main_ti = threadinfo::make(threadinfo::TI_MAIN, -1);
    main_ti->pthread() = pthread_self();
    globalepoch = active_epoch = timestamp() >> 16;
    for (int i = 0; i < (int) arraysize(test_thread_map); ++i)
        if (strcmp(test_thread_map[i].treetype, treetype) == 0) {
            current_test_name = test;
            current_trial = trial;
            test_thread_map[i].setup_func(main_ti, test_thread_initialize);
            runtest(tcpthreads, test_thread_map[i].go_func);
            if (tree_stats)
                test_thread_map[i].setup_func(main_ti, test_thread_stats);
            test_thread_map[i].setup_func(main_ti, test_thread_destroy);
            break;
        }
}

static void run_one_test(int trial, const char *treetype, const char *test,
                         const int *collectorpipe, int nruns) {
    if (nruns == 1)
        run_one_test_body(trial, treetype, test);
    else {
        pid_t c = fork();
        if (c == 0) {
            close(collectorpipe[0]);
            run_one_test_body(trial, treetype, test);
            exit(0);
        } else
            while (waitpid(c, 0, 0) == -1 && errno == EINTR)
                /* loop */;
    }
}

static double level(const std::vector<double> &v, double frac) {
    frac *= v.size() - 1;
    int base = (int) frac;
    if (base == frac)
        return v[base];
    else
        return v[base] * (1 - (frac - base)) + v[base + 1] * (frac - base);
}

static String experiment_test_table_trial(const String &key) {
    const char *l = key.begin(), *r = key.end();
    if (l + 2 < r && l[0] == 'x' && isdigit((unsigned char) l[1])) {
        for (const char *s = l; s != r; ++s)
            if (*s == '/') {
                l = s + 1;
                break;
            }
    }
    return key.substring(l, r);
}

static String experiment_run_test_table(const String &key) {
    const char *l = key.begin(), *r = key.end();
    for (const char *s = r; s != l; --s)
        if (s[-1] == '/') {
            r = s - 1;
            break;
        } else if (!isdigit((unsigned char) s[-1]))
            break;
    return key.substring(l, r);
}

static String experiment_test_table(const String &key) {
    return experiment_run_test_table(experiment_test_table_trial(key));
}

namespace {
struct gnuplot_info {
    static constexpr double trialdelta = 0.015, treetypedelta = 0.04,
        testdelta = 0.08, typedelta = 0.2;
    double pos;
    double nextdelta;
    double normalization;
    String last_test;
    int normalizetype;

    std::vector<lcdf::StringAccum> candlesticks;
    std::vector<lcdf::StringAccum> medians;
    lcdf::StringAccum xtics;

    gnuplot_info(int nt)
        : pos(1 - trialdelta), nextdelta(trialdelta), normalization(-1),
          normalizetype(nt) {
    }
    void one(const String &xname, int ti, const String &datatype_name);
    void print(FILE *f, const char * const *types_begin);
};
constexpr double gnuplot_info::trialdelta, gnuplot_info::treetypedelta, gnuplot_info::testdelta, gnuplot_info::typedelta;

void gnuplot_info::one(const String &xname, int ti, const String &datatype_name)
{
    String current_test = experiment_test_table(xname);
    if (current_test != last_test) {
        last_test = current_test;
        if (normalizetype == normtype_pertest)
            normalization = -1;
        if (nextdelta == treetypedelta)
            nextdelta = testdelta;
    }
    double beginpos = pos, firstpos = pos + nextdelta;

    std::vector<int> trials;
    for (Json::object_iterator it = experiment_stats.obegin();
         it != experiment_stats.oend(); ++it) {
        String key = it.key();
        if (experiment_run_test_table(key) == xname)
            trials.push_back(strtol(key.c_str() + xname.length() + 1, 0, 0));
    }
    std::sort(trials.begin(), trials.end());

    for (std::vector<int>::iterator tit = trials.begin();
         tit != trials.end(); ++tit) {
        Json &this_trial = experiment_stats[xname + "/" + String(*tit)];
        std::vector<double> values;
        for (int jn = 0; jn < this_trial.size(); ++jn)
            if (this_trial[jn].get(datatype_name).is_number())
                values.push_back(this_trial[jn].get(datatype_name).to_d());
        if (values.size()) {
            pos += nextdelta;
            std::sort(values.begin(), values.end());
            if (normalization < 0)
                normalization = normalizetype == normtype_none ? 1 : level(values, 0.5);
            if (int(candlesticks.size()) <= ti) {
                candlesticks.resize(ti + 1);
                medians.resize(ti + 1);
            }
            candlesticks[ti] << pos << " " << level(values, 0)
                             << " " << level(values, 0.25)
                             << " " << level(values, 0.75)
                             << " " << level(values, 1)
                             << " " << normalization << "\n";
            medians[ti] << pos << " " << level(values, 0.5) << " " << normalization << "\n";
            nextdelta = trialdelta;
        }
    }

    if (pos > beginpos) {
        double middle = (firstpos + pos) / 2;
        xtics << (xtics ? ", " : "") << "\"" << xname << "\" " << middle;
        nextdelta = treetypedelta;
    }
}

void gnuplot_info::print(FILE *f, const char * const *types_begin) {
    std::vector<int> linetypes(medians.size(), 0);
    int next_linetype = 1;
    for (int i = 0; i < int(medians.size()); ++i)
        if (medians[i])
            linetypes[i] = next_linetype++;
    struct utsname name;
    fprintf(f, "set title \"%s (%d cores)\"\n",
            (uname(&name) == 0 ? name.nodename : "unknown"),
            udpthreads);
    fprintf(f, "set terminal png\n");
    fprintf(f, "set xrange [%g:%g]\n", 1 - treetypedelta, pos + treetypedelta);
    if (gnuplot_yrange)
        fprintf(f, "set yrange [%s]\n", gnuplot_yrange.c_str());
    fprintf(f, "set xtics rotate by 45 right (%s) font \"Verdana,9\"\n", xtics.c_str());
    fprintf(f, "set key top left Left reverse\n");
    if (normalizetype == normtype_none)
        fprintf(f, "set ylabel \"count\"\n");
    else if (normalizetype == normtype_pertest)
        fprintf(f, "set ylabel \"count, normalized per test\"\n");
    else
        fprintf(f, "set ylabel \"normalized count (1=%f)\"\n", normalization);
    const char *sep = "plot ";
    for (int i = 0; i < int(medians.size()); ++i)
        if (medians[i]) {
            fprintf(f, "%s '-' using 1:($3/$6):($2/$6):($5/$6):($4/$6) with candlesticks lt %d title '%s', \\\n",
                    sep, linetypes[i], types_begin[i]);
            fprintf(f, " '-' using 1:($2/$3):($2/$3):($2/$3):($2/$3) with candlesticks lt %d notitle", linetypes[i]);
            sep = ", \\\n";
        }
    fprintf(f, "\n");
    for (int i = 0; i < int(medians.size()); ++i)
        if (medians[i]) {
            fwrite(candlesticks[i].begin(), 1, candlesticks[i].length(), f);
            fprintf(f, "e\n");
            fwrite(medians[i].begin(), 1, medians[i].length(), f);
            fprintf(f, "e\n");
        }
}

}

static void print_gnuplot(FILE *f, const char * const *types_begin, const char * const *types_end,
                          std::vector<String> &comparisons, int normalizetype) {
    for (std::vector<String>::iterator cit = comparisons.begin();
         cit != comparisons.end(); ++cit) {
        if (!*cit)
            *cit = "[^x]*";
        else if (cit->length() >= 2 && (*cit)[0] == 'x' && isdigit((unsigned char) (*cit)[1]))
            *cit += String(cit->find_left('/') < 0 ? "/*" : "*");
        else
            *cit = String("x*") + *cit + String("*");
    }

    std::vector<String> all_versions, all_experiments;
    for (Json::object_iterator it = experiment_stats.obegin();
         it != experiment_stats.oend(); ++it)
        for (std::vector<String>::const_iterator cit = comparisons.begin();
             cit != comparisons.end(); ++cit)
            if (it.key().glob_match(*cit)) {
                all_experiments.push_back(experiment_run_test_table(it.key()));
                all_versions.push_back(experiment_test_table(it.key()));
                break;
            }
    std::sort(all_experiments.begin(), all_experiments.end());
    all_experiments.erase(std::unique(all_experiments.begin(), all_experiments.end()),
                          all_experiments.end());
    std::sort(all_versions.begin(), all_versions.end());
    all_versions.erase(std::unique(all_versions.begin(), all_versions.end()),
                       all_versions.end());

    int ntypes = (int) (types_end - types_begin);
    gnuplot_info gpinfo(normalizetype);

    for (int ti = 0; ti < ntypes; ++ti) {
        double typepos = gpinfo.pos;
        for (std::vector<String>::iterator vit = all_versions.begin();
             vit != all_versions.end(); ++vit) {
            for (std::vector<String>::iterator xit = all_experiments.begin();
                 xit != all_experiments.end(); ++xit)
                if (experiment_test_table(*xit) == *vit)
                    gpinfo.one(*xit, ti, types_begin[ti]);
        }
        if (gpinfo.pos > typepos)
            gpinfo.nextdelta = gpinfo.typedelta;
        gpinfo.last_test = "";
    }

    if (gpinfo.xtics)
        gpinfo.print(f, types_begin);
}

static String
read_file(FILE *f, const char *name)
{
    lcdf::StringAccum sa;
    while (1) {
        size_t x = fread(sa.reserve(4096), 1, 4096, f);
        if (x != 0)
            sa.adjust_length(x);
        else if (ferror(f)) {
            fprintf(stderr, "%s: %s\n", name, strerror(errno));
            return String::make_stable("???", 3);
        } else
            return sa.take_string();
    }
}

static void
update_labnotebook(String notebook)
{
    FILE *f = (notebook == "-" ? stdin : fopen(notebook.c_str(), "r"));
    String previous_text = (f ? read_file(f, notebook.c_str()) : String());
    if (previous_text.out_of_memory())
        return;
    if (f && f != stdin)
        fclose(f);

    Json nb = Json::parse(previous_text);
    if (previous_text && (!nb.is_object() || !nb["experiments"])) {
        fprintf(stderr, "%s: unexpected contents, not writing new data\n", notebook.c_str());
        return;
    }

    if (!nb)
        nb = Json::make_object();
    if (!nb.get("experiments"))
        nb.set("experiments", Json::make_object());
    if (!nb.get("data"))
        nb.set("data", Json::make_object());

    Json old_data = nb["data"];
    if (!experiment_stats) {
        experiment_stats = old_data;
        return;
    }

    Json xjson;

    FILE *git_info_p = popen("git rev-parse HEAD | tr -d '\n'; git --no-pager diff --exit-code --shortstat HEAD >/dev/null 2>&1 || echo M", "r");
    String git_info = read_file(git_info_p, "<git output>");
    pclose(git_info_p);
    if (git_info)
        xjson.set("git-revision", git_info.trim());

    time_t now = time(0);
    xjson.set("time", String(ctime(&now)).trim());
    if (gid)
        xjson.set("gid", String(gid));

    struct utsname name;
    if (uname(&name) == 0)
        xjson.set("machine", name.nodename);

    xjson.set("cores", udpthreads);

    Json &runs = xjson.get_insert("runs");
    String xname = "x" + String(nb["experiments"].size());
    for (Json::const_iterator it = experiment_stats.begin();
         it != experiment_stats.end(); ++it) {
        String xkey = xname + "/" + it.key();
        runs.push_back(xkey);
        nb["data"][xkey] = it.value();
    }
    xjson.set("runs", runs);

    nb["experiments"][xname] = xjson;

    String new_text = nb.unparse(Json::indent_depth(4).tab_width(2).newline_terminator(true));
    f = (notebook == "-" ? stdout : fopen((notebook + "~").c_str(), "w"));
    if (!f) {
        fprintf(stderr, "%s~: %s\n", notebook.c_str(), strerror(errno));
        return;
    }
    size_t written = fwrite(new_text.data(), 1, new_text.length(), f);
    if (written != size_t(new_text.length())) {
        fprintf(stderr, "%s~: %s\n", notebook.c_str(), strerror(errno));
        fclose(f);
        return;
    }
    if (f != stdout) {
        fclose(f);
        if (rename((notebook + "~").c_str(), notebook.c_str()) != 0)
            fprintf(stderr, "%s: %s\n", notebook.c_str(), strerror(errno));
    }

    fprintf(stderr, "EXPERIMENT %s\n", xname.c_str());
    experiment_stats.merge(old_data);
}

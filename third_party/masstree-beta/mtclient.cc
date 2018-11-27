/* Masstree
 * Eddie Kohler, Yandong Mao, Robert Morris
 * Copyright (c) 2012-2014 President and Fellows of Harvard College
 * Copyright (c) 2012-2014 Massachusetts Institute of Technology
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <limits.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <math.h>
#include <fcntl.h>
#include "kvstats.hh"
#include "kvio.hh"
#include "json.hh"
#include "kvtest.hh"
#include "mtclient.hh"
#include "kvrandom.hh"
#include "clp.h"

const char *serverip = "127.0.0.1";

typedef void (*get_async_cb)(struct child *c, struct async *a,
                             bool has_val, const Str &val);
typedef void (*put_async_cb)(struct child *c, struct async *a,
                             int status);
typedef void (*remove_async_cb)(struct child *c, struct async *a,
                                int status);

struct async {
    int cmd; // Cmd_ constant
    unsigned seq;
    union {
        get_async_cb get_fn;
        put_async_cb put_fn;
        remove_async_cb remove_fn;
    };
    char key[16]; // just first 16 bytes
    char wanted[16]; // just first 16 bytes
    int wantedlen;
    int acked;
};
#define MAXWINDOW 512
unsigned window = MAXWINDOW;

struct child {
    int s;
    int udp; // 1 -> udp, 0 -> tcp
    KVConn *conn;

    struct async a[MAXWINDOW];

    unsigned seq0_;
    unsigned seq1_;
    unsigned long long nsent_;
    int childno;

    inline void check_flush();
};

void checkasync(struct child *c, int force);

inline void child::check_flush() {
    if ((seq1_ & ((window - 1) >> 1)) == 0)
        conn->flush();
    while (seq1_ - seq0_ >= window)
        checkasync(this, 1);
}

void aget(struct child *, const Str &key, const Str &wanted, get_async_cb fn);
void aget(struct child *c, long ikey, long iwanted, get_async_cb fn);
void aget_col(struct child *c, const Str& key, int col, const Str& wanted,
              get_async_cb fn);
int get(struct child *c, const Str &key, char *val, int max);

void asyncgetcb(struct child *, struct async *a, bool, const Str &val);
void asyncgetcb_int(struct child *, struct async *a, bool, const Str &val);

void aput(struct child *c, const Str &key, const Str &val,
          put_async_cb fn = 0, const Str &wanted = Str());
void aput_col(struct child *c, const Str &key, int col, const Str &val,
              put_async_cb fn = 0, const Str &wanted = Str());
int put(struct child *c, const Str &key, const Str &val);
void asyncputcb(struct child *, struct async *a, int status);

void aremove(struct child *c, const Str &key, remove_async_cb fn);
bool remove(struct child *c, const Str &key);

void udp1(struct child *);
void w1b(struct child *);
void u1(struct child *);
void over1(struct child *);
void over2(struct child *);
void rec1(struct child *);
void rec2(struct child *);
void cpa(struct child *);
void cpb(struct child *);
void cpc(struct child *);
void cpd(struct child *);
void volt1a(struct child *);
void volt1b(struct child *);
void volt2a(struct child *);
void volt2b(struct child *);
void scantest(struct child *);

static int children = 1;
static uint64_t nkeys = 0;
static int prefixLen = 0;
static int keylen = 0;
static uint64_t limit = ~uint64_t(0);
double duration = 10;
double duration2 = 0;
int udpflag = 0;
int quiet = 0;
int first_server_port = 2117;
// Should all child processes connects to the same UDP PORT on server
bool share_server_port = false;
volatile bool timeout[2] = {false, false};
int first_local_port = 0;
const char *input = NULL;
static int rsinit_part = 0;
int kvtest_first_seed = 0;
static int rscale_partsz = 0;
static int getratio = -1;
static int minkeyletter = '0';
static int maxkeyletter = '9';


struct kvtest_client {
    kvtest_client(struct child& c)
        : c_(&c) {
    }
    struct child* child() const {
        return c_;
    }
    int id() const {
        return c_->childno;
    }
    int nthreads() const {
        return ::children;
    }
    char minkeyletter() const {
        return ::minkeyletter;
    }
    char maxkeyletter() const {
        return ::maxkeyletter;
    }
    void register_timeouts(int n) {
        (void) n;
    }
    bool timeout(int which) const {
        return ::timeout[which];
    }
    uint64_t limit() const {
        return ::limit;
    }
    int getratio() const {
        assert(::getratio >= 0);
        return ::getratio;
    }
    uint64_t nkeys() const {
        return ::nkeys;
    }
    int keylen() const {
        return ::keylen;
    }
    int prefixLen() const {
        return ::prefixLen;
    }
    double now() const {
        return ::now();
    }

    void get(long ikey, Str *value) {
        quick_istr key(ikey);
        aget(c_, key.string(),
             Str(reinterpret_cast<const char *>(&value), sizeof(value)),
             asyncgetcb);
    }
    void get(const Str &key, int *ivalue) {
        aget(c_, key,
             Str(reinterpret_cast<const char *>(&ivalue), sizeof(ivalue)),
             asyncgetcb_int);
    }
    bool get_sync(long ikey) {
        char got[512];
        quick_istr key(ikey);
        return ::get(c_, key.string(), got, sizeof(got)) >= 0;
    }
    void get_check(long ikey, long iexpected) {
        aget(c_, ikey, iexpected, 0);
    }
    void get_check(const char *key, const char *val) {
        aget(c_, Str(key), Str(val), 0);
    }
    void get_check(const Str &key, const Str &val) {
        aget(c_, key, val, 0);
    }
    void get_check_key8(long ikey, long iexpected) {
        quick_istr key(ikey, 8), expected(iexpected);
        aget(c_, key.string(), expected.string(), 0);
    }
    void get_check_key10(long ikey, long iexpected) {
        quick_istr key(ikey, 10), expected(iexpected);
        aget(c_, key.string(), expected.string(), 0);
    }
    void many_get_check(int, long [], long []) {
        assert(0);
    }
    void get_col_check(const Str &key, int col, const Str &value) {
        aget_col(c_, key, col, value, 0);
    }
    void get_col_check(long ikey, int col, long ivalue) {
        quick_istr key(ikey), value(ivalue);
        get_col_check(key.string(), col, value.string());
    }
    void get_col_check_key10(long ikey, int col, long ivalue) {
        quick_istr key(ikey, 10), value(ivalue);
        get_col_check(key.string(), col, value.string());
    }
    void get_check_sync(long ikey, long iexpected) {
        char key[512], val[512], got[512];
        sprintf(key, "%010ld", ikey);
        sprintf(val, "%ld", iexpected);
        memset(got, 0, sizeof(got));
        ::get(c_, Str(key), got, sizeof(got));
        if (strcmp(val, got)) {
            fprintf(stderr, "key %s, expected %s, got %s\n", key, val, got);
            always_assert(0);
        }
    }

    void put(const Str &key, const Str &value) {
        aput(c_, key, value);
    }
    void put(const Str &key, const Str &value, int *status) {
        aput(c_, key, value,
             asyncputcb,
             Str(reinterpret_cast<const char *>(&status), sizeof(status)));
    }
    void put(const char *key, const char *value) {
        aput(c_, Str(key), Str(value));
    }
    void put(const Str &key, long ivalue) {
        quick_istr value(ivalue);
        aput(c_, key, value.string());
    }
    void put(long ikey, long ivalue) {
        quick_istr key(ikey), value(ivalue);
        aput(c_, key.string(), value.string());
    }
    void put_key8(long ikey, long ivalue) {
        quick_istr key(ikey, 8), value(ivalue);
        aput(c_, key.string(), value.string());
    }
    void put_key10(long ikey, long ivalue) {
        quick_istr key(ikey, 10), value(ivalue);
        aput(c_, key.string(), value.string());
    }
    void put_col(const Str &key, int col, const Str &value) {
        aput_col(c_, key, col, value);
    }
    void put_col(long ikey, int col, long ivalue) {
        quick_istr key(ikey), value(ivalue);
        put_col(key.string(), col, value.string());
    }
    void put_col_key10(long ikey, int col, long ivalue) {
        quick_istr key(ikey, 10), value(ivalue);
        put_col(key.string(), col, value.string());
    }
    void put_sync(long ikey, long ivalue) {
        quick_istr key(ikey, 10), value(ivalue);
        ::put(c_, key.string(), value.string());
    }

    void remove(const Str &key) {
        aremove(c_, key, 0);
    }
    void remove(long ikey) {
        quick_istr key(ikey);
        remove(key.string());
    }
    bool remove_sync(long ikey) {
        quick_istr key(ikey);
        return ::remove(c_, key.string());
    }

    int ruscale_partsz() const {
        return ::rscale_partsz;
    }
    int ruscale_init_part_no() const {
        return ::rsinit_part;
    }
    long nseqkeys() const {
        return 16 * ::rscale_partsz;
    }
    void wait_all() {
        checkasync(c_, 2);
    }
    void puts_done() {
    }
    void rcu_quiesce() {
    }
    void notice(String s) {
        if (!quiet) {
            if (!s.empty() && s.back() == '\n')
                s = s.substr(0, -1);
            if (s.empty() || isspace((unsigned char) s[0]))
                fprintf(stderr, "%d%.*s\n", c_->childno, s.length(), s.data());
            else
                fprintf(stderr, "%d %.*s\n", c_->childno, s.length(), s.data());
        }
    }
    void notice(const char *fmt, ...) {
        if (!quiet) {
            va_list val;
            va_start(val, fmt);
            String x;
            if (!*fmt || isspace((unsigned char) *fmt))
                x = String(c_->childno) + fmt;
            else
                x = String(c_->childno) + String(" ") + fmt;
            vfprintf(stderr, x.c_str(), val);
            va_end(val);
        }
    }
    const Json& report(const Json& x) {
        return report_.merge(x);
    }
    void finish() {
        if (!quiet) {
            lcdf::StringAccum sa;
            double dv;
            if (report_.count("puts"))
                sa << " total " << report_.get("puts");
            if (report_.get("puts_per_sec", dv))
                sa.snprintf(100, " %.0f put/s", dv);
            if (report_.get("gets_per_sec", dv))
                sa.snprintf(100, " %.0f get/s", dv);
            if (!sa.empty())
                notice(sa.take_string());
        }
        printf("%s\n", report_.unparse().c_str());
    }
    kvrandom_random rand;
    struct child *c_;
    Json report_;
};


#define TESTRUNNER_CLIENT_TYPE kvtest_client&
#include "testrunner.hh"

MAKE_TESTRUNNER(rw1, kvtest_rw1(client));
MAKE_TESTRUNNER(rw2, kvtest_rw2(client));
MAKE_TESTRUNNER(rw3, kvtest_rw3(client));
MAKE_TESTRUNNER(rw4, kvtest_rw4(client));
MAKE_TESTRUNNER(rw1fixed, kvtest_rw1fixed(client));
MAKE_TESTRUNNER(rw16, kvtest_rw16(client));
MAKE_TESTRUNNER(sync_rw1, kvtest_sync_rw1(client));
MAKE_TESTRUNNER(r1, kvtest_r1_seed(client, kvtest_first_seed + client.id()));
MAKE_TESTRUNNER(w1, kvtest_w1_seed(client, kvtest_first_seed + client.id()));
MAKE_TESTRUNNER(w1b, w1b(client.child()));
MAKE_TESTRUNNER(u1, u1(client.child()));
MAKE_TESTRUNNER(wd1, kvtest_wd1(10000000, 1, client));
MAKE_TESTRUNNER(wd1m1, kvtest_wd1(100000000, 1, client));
MAKE_TESTRUNNER(wd1m2, kvtest_wd1(1000000000, 4, client));
MAKE_TESTRUNNER(wd1check, kvtest_wd1_check(10000000, 1, client));
MAKE_TESTRUNNER(wd1m1check, kvtest_wd1_check(100000000, 1, client));
MAKE_TESTRUNNER(wd1m2check, kvtest_wd1_check(1000000000, 4, client));
MAKE_TESTRUNNER(wd2, kvtest_wd2(client));
MAKE_TESTRUNNER(wd2check, kvtest_wd2_check(client));
MAKE_TESTRUNNER(tri1, kvtest_tri1(10000000, 1, client));
MAKE_TESTRUNNER(tri1check, kvtest_tri1_check(10000000, 1, client));
MAKE_TESTRUNNER(same, kvtest_same(client));
MAKE_TESTRUNNER(wcol1, kvtest_wcol1at(client, client.id() % 24, kvtest_first_seed + client.id() % 48, 5000000));
MAKE_TESTRUNNER(rcol1, kvtest_rcol1at(client, client.id() % 24, kvtest_first_seed + client.id() % 48, 5000000));
MAKE_TESTRUNNER(wcol1o1, kvtest_wcol1at(client, (client.id() + 1) % 24, kvtest_first_seed + client.id() % 48, 5000000));
MAKE_TESTRUNNER(rcol1o1, kvtest_rcol1at(client, (client.id() + 1) % 24, kvtest_first_seed + client.id() % 48, 5000000));
MAKE_TESTRUNNER(wcol1o2, kvtest_wcol1at(client, (client.id() + 2) % 24, kvtest_first_seed + client.id() % 48, 5000000));
MAKE_TESTRUNNER(rcol1o2, kvtest_rcol1at(client, (client.id() + 2) % 24, kvtest_first_seed + client.id() % 48, 5000000));
MAKE_TESTRUNNER(over1, over1(client.child()));
MAKE_TESTRUNNER(over2, over2(client.child()));
MAKE_TESTRUNNER(rec1, rec1(client.child()));
MAKE_TESTRUNNER(rec2, rec2(client.child()));
MAKE_TESTRUNNER(cpa, cpa(client.child()));
MAKE_TESTRUNNER(cpb, cpb(client.child()));
MAKE_TESTRUNNER(cpc, cpc(client.child()));
MAKE_TESTRUNNER(cpd, cpd(client.child()));
MAKE_TESTRUNNER(volt1a, volt1a(client.child()));
MAKE_TESTRUNNER(volt1b, volt1b(client.child()));
MAKE_TESTRUNNER(volt2a, volt2a(client.child()));
MAKE_TESTRUNNER(volt2b, volt2b(client.child()));
MAKE_TESTRUNNER(scantest, scantest(client.child()));
MAKE_TESTRUNNER(wscale, kvtest_wscale(client));
MAKE_TESTRUNNER(ruscale_init, kvtest_ruscale_init(client));
MAKE_TESTRUNNER(rscale, kvtest_rscale(client));
MAKE_TESTRUNNER(uscale, kvtest_uscale(client));
MAKE_TESTRUNNER(long_init, kvtest_long_init(client));
MAKE_TESTRUNNER(long_go, kvtest_long_go(client));
MAKE_TESTRUNNER(udp1, kvtest_udp1(client));

void run_child(testrunner*, int childno);


void
usage()
{
  fprintf(stderr, "Usage: mtclient [-s serverip] [-w window] [--udp] "\
          "[-j nchildren] [-d duration] [--ssp] [--flp first_local_port] "\
          "[--fsp first_server_port] [-i json_input]\nTests:\n");
  testrunner::print_names(stderr, 5);
  exit(1);
}

void
settimeout(int)
{
  if (!timeout[0]) {
    timeout[0] = true;
    if (duration2)
        alarm((int) ceil(duration2));
  } else
    timeout[1] = true;
}

enum { clp_val_suffixdouble = Clp_ValFirstUser };
enum { opt_threads = 1, opt_threads_deprecated, opt_duration, opt_duration2,
       opt_window, opt_server, opt_first_server_port, opt_quiet, opt_udp,
       opt_first_local_port, opt_share_server_port, opt_input,
       opt_rsinit_part, opt_first_seed, opt_rscale_partsz, opt_keylen,
       opt_limit, opt_prefix_len, opt_nkeys, opt_get_ratio, opt_minkeyletter,
       opt_maxkeyletter, opt_nofork };
static const Clp_Option options[] = {
    { "threads", 'j', opt_threads, Clp_ValInt, 0 },
    { 0, 'n', opt_threads_deprecated, Clp_ValInt, 0 },
    { "duration", 'd', opt_duration, Clp_ValDouble, 0 },
    { "duration2", 0, opt_duration2, Clp_ValDouble, 0 },
    { "d2", 0, opt_duration2, Clp_ValDouble, 0 },
    { "window", 'w', opt_window, Clp_ValUnsigned, 0 },
    { "server-ip", 's', opt_server, Clp_ValString, 0 },
    { "first-server-port", 0, opt_first_server_port, Clp_ValInt, 0 },
    { "fsp", 0, opt_first_server_port, Clp_ValInt, 0 },
    { "quiet", 'q', opt_quiet, 0, Clp_Negate },
    { "udp", 'u', opt_udp, 0, Clp_Negate },
    { "first-local-port", 0, opt_first_local_port, Clp_ValInt, 0 },
    { "flp", 0, opt_first_local_port, Clp_ValInt, 0 },
    { "share-server-port", 0, opt_share_server_port, 0, Clp_Negate },
    { "ssp", 0, opt_share_server_port, 0, Clp_Negate },
    { "input", 'i', opt_input, Clp_ValString, 0 },
    { "rsinit_part", 0, opt_rsinit_part, Clp_ValInt, 0 },
    { "first_seed", 0, opt_first_seed, Clp_ValInt, 0 },
    { "rscale_partsz", 0, opt_rscale_partsz, Clp_ValInt, 0 },
    { "keylen", 0, opt_keylen, Clp_ValInt, 0 },
    { "limit", 'l', opt_limit, clp_val_suffixdouble, 0 },
    { "prefixLen", 0, opt_prefix_len, Clp_ValInt, 0 },
    { "nkeys", 0, opt_nkeys, Clp_ValInt, 0 },
    { "getratio", 0, opt_get_ratio, Clp_ValInt, 0 },
    { "minkeyletter", 0, opt_minkeyletter, Clp_ValString, 0 },
    { "maxkeyletter", 0, opt_maxkeyletter, Clp_ValString, 0 },
    { "no-fork", 0, opt_nofork, 0, 0 }
};

int
main(int argc, char *argv[])
{
  int i, pid, status;
  testrunner* test = 0;
  int pipes[512];
  int dofork = 1;

  Clp_Parser *clp = Clp_NewParser(argc, argv, (int) arraysize(options), options);
  Clp_AddType(clp, clp_val_suffixdouble, Clp_DisallowOptions, clp_parse_suffixdouble, 0);
  int opt;
  while ((opt = Clp_Next(clp)) != Clp_Done) {
      switch (opt) {
      case opt_threads:
          children = clp->val.i;
          break;
      case opt_threads_deprecated:
          Clp_OptionError(clp, "%<%O%> is deprecated, use %<-j%>");
          children = clp->val.i;
          break;
      case opt_duration:
          duration = clp->val.d;
          break;
      case opt_duration2:
          duration2 = clp->val.d;
          break;
      case opt_window:
          window = clp->val.u;
          always_assert(window <= MAXWINDOW);
          always_assert((window & (window - 1)) == 0); // power of 2
          break;
      case opt_server:
          serverip = clp->vstr;
          break;
      case opt_first_server_port:
          first_server_port = clp->val.i;
          break;
      case opt_quiet:
          quiet = !clp->negated;
          break;
      case opt_udp:
          udpflag = !clp->negated;
          break;
      case opt_first_local_port:
          first_local_port = clp->val.i;
          break;
      case opt_share_server_port:
          share_server_port = !clp->negated;
          break;
      case opt_input:
          input = clp->vstr;
          break;
      case opt_rsinit_part:
          rsinit_part = clp->val.i;
          break;
      case opt_first_seed:
          kvtest_first_seed = clp->val.i;
          break;
      case opt_rscale_partsz:
          rscale_partsz = clp->val.i;
          break;
      case opt_keylen:
          keylen = clp->val.i;
          break;
      case opt_limit:
          limit = (uint64_t) clp->val.d;
          break;
      case opt_prefix_len:
          prefixLen = clp->val.i;
          break;
      case opt_nkeys:
          nkeys = clp->val.i;
          break;
      case opt_get_ratio:
          getratio = clp->val.i;
          break;
      case opt_minkeyletter:
          minkeyletter = clp->vstr[0];
          break;
      case opt_maxkeyletter:
          maxkeyletter = clp->vstr[0];
          break;
      case opt_nofork:
          dofork = !clp->negated;
          break;
      case Clp_NotOption:
          test = testrunner::find(clp->vstr);
          if (!test)
              usage();
          break;
      case Clp_BadOption:
          usage();
          break;
      }
  }
  if(children < 1 || (children != 1 && !dofork))
    usage();
  if (!test)
      test = testrunner::first();

  printf("%s, w %d, test %s, children %d\n",
         udpflag ? "udp" : "tcp", window,
         test->name().c_str(), children);

  fflush(stdout);

  if (dofork) {
      for(i = 0; i < children; i++){
          int ptmp[2];
          int r = pipe(ptmp);
          always_assert(r == 0);
          pid = fork();
          if(pid < 0){
              perror("fork");
              exit(1);
          }
          if(pid == 0){
              close(ptmp[0]);
              dup2(ptmp[1], 1);
              close(ptmp[1]);
              signal(SIGALRM, settimeout);
              alarm((int) ceil(duration));
              run_child(test, i);
              exit(0);
          }
          pipes[i] = ptmp[0];
          close(ptmp[1]);
      }
      for(i = 0; i < children; i++){
          if(wait(&status) <= 0){
              perror("wait");
              exit(1);
          }
          if (WIFSIGNALED(status))
              fprintf(stderr, "child %d died by signal %d\n", i, WTERMSIG(status));
      }
  } else {
      int ptmp[2];
      int r = pipe(ptmp);
      always_assert(r == 0);
      pipes[0] = ptmp[0];
      int stdout_fd = dup(STDOUT_FILENO);
      always_assert(stdout_fd > 0);
      r = dup2(ptmp[1], STDOUT_FILENO);
      always_assert(r >= 0);
      close(ptmp[1]);
      signal(SIGALRM, settimeout);
      alarm((int) ceil(duration));
      run_child(test, 0);
      fflush(stdout);
      r = dup2(stdout_fd, STDOUT_FILENO);
      always_assert(r >= 0);
      close(stdout_fd);
  }

  long long total = 0;
  kvstats puts, gets, scans, puts_per_sec, gets_per_sec, scans_per_sec;
  for(i = 0; i < children; i++){
    char buf[2048];
    int cc = read(pipes[i], buf, sizeof(buf)-1);
    assert(cc > 0);
    buf[cc] = 0;
    printf("%s", buf);
    Json bufj = Json::parse(buf, buf + cc);
    long long iv;
    double dv;
    if (bufj.to_i(iv))
        total += iv;
    else if (bufj.is_object()) {
        if (bufj.get("ops", iv)
            || bufj.get("total", iv)
            || bufj.get("count", iv))
            total += iv;
        if (bufj.get("puts", iv))
            puts.add(iv);
        if (bufj.get("gets", iv))
            gets.add(iv);
        if (bufj.get("scans", iv))
            scans.add(iv);
        if (bufj.get("puts_per_sec", dv))
            puts_per_sec.add(dv);
        if (bufj.get("gets_per_sec", dv))
            gets_per_sec.add(dv);
        if (bufj.get("scans_per_sec", dv))
            scans_per_sec.add(dv);
    }
  }

  printf("total %lld\n", total);
  puts.print_report("puts");
  gets.print_report("gets");
  scans.print_report("scans");
  puts_per_sec.print_report("puts/s");
  gets_per_sec.print_report("gets/s");
  scans_per_sec.print_report("scans/s");

  exit(0);
}

void
run_child(testrunner* test, int childno)
{
  struct sockaddr_in sin;
  int ret, yes = 1;
  struct child c;

  bzero(&c, sizeof(c));
  c.childno = childno;

  if(udpflag){
    c.udp = 1;
    c.s = socket(AF_INET, SOCK_DGRAM, 0);
  } else {
    c.s = socket(AF_INET, SOCK_STREAM, 0);
  }
  if (first_local_port) {
    bzero(&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(first_local_port + (childno % 48));
    ret = ::bind(c.s, (struct sockaddr *) &sin, sizeof(sin));
    if (ret < 0) {
      perror("bind");
      exit(1);
    }
  }

  assert(c.s >= 0);
  setsockopt(c.s, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

  bzero(&sin, sizeof(sin));
  sin.sin_family = AF_INET;
  if (udpflag && !share_server_port)
    sin.sin_port = htons(first_server_port + (childno % 48));
  else
    sin.sin_port = htons(first_server_port);
  sin.sin_addr.s_addr = inet_addr(serverip);
  ret = connect(c.s, (struct sockaddr *) &sin, sizeof(sin));
  if(ret < 0){
    perror("connect");
    exit(1);
  }

  c.conn = new KVConn(c.s, !udpflag);
  kvtest_client client(c);

  test->run(client);

  checkasync(&c, 2);

  delete c.conn;
  close(c.s);
}

void KVConn::hard_check(int tryhard) {
    masstree_precondition(inbufpos_ == inbuflen_);
    if (parser_.empty()) {
        inbufpos_ = inbuflen_ = 0;
        for (auto x : oldinbuf_)
            delete[] x;
        oldinbuf_.clear();
    } else if (inbufpos_ == inbufsz) {
        oldinbuf_.push_back(inbuf_);
        inbuf_ = new char[inbufsz];
        inbufpos_ = inbuflen_ = 0;
    }
    if (tryhard == 1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(infd_, &rfds);
        struct timeval tv = {0, 0};
        if (select(infd_ + 1, &rfds, NULL, NULL, &tv) <= 0)
            return;
    } else
        kvflush(out_);

    ssize_t r = read(infd_, inbuf_ + inbufpos_, inbufsz - inbufpos_);
    if (r != -1)
        inbuflen_ += r;
}

int
get(struct child *c, const Str &key, char *val, int max)
{
    assert(c->seq0_ == c->seq1_);

    unsigned sseq = c->seq1_;
    ++c->seq1_;
    ++c->nsent_;

    c->conn->sendgetwhole(key, sseq);
    c->conn->flush();

    const Json& result = c->conn->receive();
    always_assert(result && result[0] == sseq);
    ++c->seq0_;
    if (!result[2])
        return -1;
    always_assert(result.size() == 3 && result[2].is_s()
                  && result[2].as_s().length() <= max);
    memcpy(val, result[2].as_s().data(), result[2].as_s().length());
    return result[2].as_s().length();
}

// builtin aget callback: no check
void
nocheck(struct child *, struct async *, bool, const Str &)
{
}

// builtin aget callback: store string
void
asyncgetcb(struct child *, struct async *a, bool, const Str &val)
{
    Str *sptr;
    assert(a->wantedlen == sizeof(Str *));
    memcpy(&sptr, a->wanted, sizeof(Str *));
    sptr->len = std::min(sptr->len, val.len);
    memcpy(const_cast<char *>(sptr->s), val.s, sptr->len);
}

// builtin aget callback: store string
void
asyncgetcb_int(struct child *, struct async *a, bool, const Str &val)
{
    int *vptr;
    assert(a->wantedlen == sizeof(int *));
    memcpy(&vptr, a->wanted, sizeof(int *));
    long x = 0;
    if (val.len <= 0)
        x = -1;
    else
        for (int i = 0; i < val.len; ++i)
            if (val.s[i] >= '0' && val.s[i] <= '9')
                x = (x * 10) + (val.s[i] - '0');
            else {
                x = -1;
                break;
            }
    *vptr = x;
}

// default aget callback: check val against wanted
void
defaultget(struct child *, struct async *a, bool have_val, const Str &val)
{
    // check that we got the expected value
    int wanted_avail = std::min(a->wantedlen, int(sizeof(a->wanted)));
    if (!have_val
        || a->wantedlen != val.len
        || memcmp(val.s, a->wanted, wanted_avail) != 0)
        fprintf(stderr, "oops wanted %.*s(%d) got %.*s(%d)\n",
                wanted_avail, a->wanted, a->wantedlen, val.len, val.s, val.len);
    else {
        always_assert(a->wantedlen == val.len);
        always_assert(memcmp(val.s, a->wanted, wanted_avail) == 0);
    }
}

// builtin aput/aremove callback: store status
void
asyncputcb(struct child *, struct async *a, int status)
{
    int *sptr;
    assert(a->wantedlen == sizeof(int *));
    memcpy(&sptr, a->wanted, sizeof(int *));
    *sptr = status;
}

// process any waiting replies to aget() and aput().
// force=0 means non-blocking check if anything waiting on socket.
// force=1 means wait for at least one reply.
// force=2 means wait for all pending (nw-nr) replies.
void
checkasync(struct child *c, int force)
{
    while (c->seq0_ != c->seq1_) {
        if (force)
            c->conn->flush();
        if (c->conn->check(force ? 2 : 1) > 0) {
            const Json& result = c->conn->receive();
            always_assert(result);

            // is rseq in the nr..nw window?
            // replies might arrive out of order if UDP
            unsigned rseq = result[0].as_i();
            always_assert(rseq - c->seq0_ < c->seq1_ - c->seq0_);
            struct async *a = &c->a[rseq & (window - 1)];
            always_assert(a->seq == rseq);

            // advance the nr..nw window
            always_assert(a->acked == 0);
            a->acked = 1;
            while (c->seq0_ != c->seq1_ && c->a[c->seq0_ & (window - 1)].acked)
                ++c->seq0_;

            // might have been the last free slot,
            // don't want to re-use it underfoot.
            struct async tmpa = *a;

            if(tmpa.cmd == Cmd_Get){
                // this is a reply to a get
                String s = result.size() > 2 ? result[2].as_s() : String();
                if (tmpa.get_fn)
                    (tmpa.get_fn)(c, &tmpa, result.size() > 2, s);
            } else if (tmpa.cmd == Cmd_Put || tmpa.cmd == Cmd_Replace) {
                // this is a reply to a put
                if (tmpa.put_fn)
                    (tmpa.put_fn)(c, &tmpa, result[2].as_i());
            } else if(tmpa.cmd == Cmd_Scan){
                // this is a reply to a scan
                always_assert((result.size() - 2) / 2 <= tmpa.wantedlen);
            } else if (tmpa.cmd == Cmd_Remove) {
                // this is a reply to a remove
                if (tmpa.remove_fn)
                    (tmpa.remove_fn)(c, &tmpa, result[2].as_i());
            } else {
                always_assert(0);
            }

            if (force < 2)
                force = 0;
        } else if (!force)
            break;
    }
}

// async get, checkasync() will eventually check reply
// against wanted.
void
aget(struct child *c, const Str &key, const Str &wanted, get_async_cb fn)
{
    c->check_flush();

    c->conn->sendgetwhole(key, c->seq1_);
    if (c->udp)
        c->conn->flush();

    struct async *a = &c->a[c->seq1_ & (window - 1)];
    a->cmd = Cmd_Get;
    a->seq = c->seq1_;
    a->get_fn = (fn ? fn : defaultget);
    assert(key.len < int(sizeof(a->key)));
    memcpy(a->key, key.s, key.len);
    a->key[key.len] = 0;
    a->wantedlen = wanted.len;
    int wantedavail = std::min(wanted.len, int(sizeof(a->wanted)));
    memcpy(a->wanted, wanted.s, wantedavail);
    a->acked = 0;

    ++c->seq1_;
    ++c->nsent_;
}

void aget_col(struct child *c, const Str& key, int col, const Str& wanted,
              get_async_cb fn)
{
    c->check_flush();

    c->conn->sendgetcol(key, col, c->seq1_);
    if (c->udp)
        c->conn->flush();

    struct async *a = &c->a[c->seq1_ & (window - 1)];
    a->cmd = Cmd_Get;
    a->seq = c->seq1_;
    a->get_fn = (fn ? fn : defaultget);
    assert(key.len < int(sizeof(a->key)));
    memcpy(a->key, key.s, key.len);
    a->key[key.len] = 0;
    a->wantedlen = wanted.len;
    int wantedavail = std::min(wanted.len, int(sizeof(a->wanted)));
    memcpy(a->wanted, wanted.s, wantedavail);
    a->acked = 0;

    ++c->seq1_;
    ++c->nsent_;
}

void
aget(struct child *c, long ikey, long iwanted, get_async_cb fn)
{
    quick_istr key(ikey), wanted(iwanted);
    aget(c, key.string(), wanted.string(), fn);
}

int
put(struct child *c, const Str &key, const Str &val)
{
    always_assert(c->seq0_ == c->seq1_);

    unsigned int sseq = c->seq1_;
    c->conn->sendputwhole(key, val, sseq);
    c->conn->flush();

    const Json& result = c->conn->receive();
    always_assert(result && result[0] == sseq);

    ++c->seq0_;
    ++c->seq1_;
    ++c->nsent_;
    return 0;
}

void
aput(struct child *c, const Str &key, const Str &val,
     put_async_cb fn, const Str &wanted)
{
    c->check_flush();

    c->conn->sendputwhole(key, val, c->seq1_);
    if (c->udp)
        c->conn->flush();

    struct async *a = &c->a[c->seq1_ & (window - 1)];
    a->cmd = Cmd_Put;
    a->seq = c->seq1_;
    assert(key.len < int(sizeof(a->key)) - 1);
    memcpy(a->key, key.s, key.len);
    a->key[key.len] = 0;
    a->put_fn = fn;
    if (fn) {
        assert(wanted.len <= int(sizeof(a->wanted)));
        a->wantedlen = wanted.len;
        memcpy(a->wanted, wanted.s, wanted.len);
    } else {
        a->wantedlen = -1;
        a->wanted[0] = 0;
    }
    a->acked = 0;

    ++c->seq1_;
    ++c->nsent_;
}

void aput_col(struct child *c, const Str &key, int col, const Str &val,
              put_async_cb fn, const Str &wanted)
{
    c->check_flush();

    c->conn->sendputcol(key, col, val, c->seq1_);
    if (c->udp)
        c->conn->flush();

    struct async *a = &c->a[c->seq1_ & (window - 1)];
    a->cmd = Cmd_Put;
    a->seq = c->seq1_;
    assert(key.len < int(sizeof(a->key)) - 1);
    memcpy(a->key, key.s, key.len);
    a->key[key.len] = 0;
    a->put_fn = fn;
    if (fn) {
        assert(wanted.len <= int(sizeof(a->wanted)));
        a->wantedlen = wanted.len;
        memcpy(a->wanted, wanted.s, wanted.len);
    } else {
        a->wantedlen = -1;
        a->wanted[0] = 0;
    }
    a->acked = 0;

    ++c->seq1_;
    ++c->nsent_;
}

bool remove(struct child *c, const Str &key)
{
    always_assert(c->seq0_ == c->seq1_);

    unsigned int sseq = c->seq1_;
    c->conn->sendremove(key, sseq);
    c->conn->flush();

    const Json& result = c->conn->receive();
    always_assert(result && result[0] == sseq);

    ++c->seq0_;
    ++c->seq1_;
    ++c->nsent_;
    return result[2].to_b();
}

void
aremove(struct child *c, const Str &key, remove_async_cb fn)
{
    c->check_flush();

    c->conn->sendremove(key, c->seq1_);
    if (c->udp)
        c->conn->flush();

    struct async *a = &c->a[c->seq1_ & (window - 1)];
    a->cmd = Cmd_Remove;
    a->seq = c->seq1_;
    assert(key.len < int(sizeof(a->key)) - 1);
    memcpy(a->key, key.s, key.len);
    a->key[key.len] = 0;
    a->acked = 0;
    a->remove_fn = fn;

    ++c->seq1_;
    ++c->nsent_;
}

int
xcompar(const void *xa, const void *xb)
{
  long *a = (long *) xa;
  long *b = (long *) xb;
  if(*a == *b)
    return 0;
  if(*a < *b)
    return -1;
  return 1;
}

// like w1, but in a binary-tree-like order that
// produces a balanced 3-wide tree.
void
w1b(struct child *c)
{
  int n;
  if (limit == ~(uint64_t) 0)
      n = 4000000;
  else
      n = std::min(limit, (uint64_t) INT_MAX);
  long *a = (long *) malloc(sizeof(long) * n);
  always_assert(a);
  char *done = (char *) malloc(n);

  srandom(kvtest_first_seed + c->childno);

  // insert in an order which causes 3-wide
  // to be balanced

  for(int i = 0; i < n; i++){
    a[i] = random();
    done[i] = 0;
  }

  qsort(a, n, sizeof(a[0]), xcompar);
  always_assert(a[0] <= a[1] && a[1] <= a[2] && a[2] <= a[3]);

  double t0 = now(), t1;

  for(int stride = n / 2; stride > 0; stride /= 2){
    for(int i = stride; i < n; i += stride){
      if(done[i] == 0){
        done[i] = 1;
        char key[512], val[512];
        sprintf(key, "%010ld", a[i]);
        sprintf(val, "%ld", a[i] + 1);
        aput(c, Str(key), Str(val));
      }
    }
  }
  for(int i = 0; i < n; i++){
    if(done[i] == 0){
      done[i] = 1;
      char key[512], val[512];
      sprintf(key, "%010ld", a[i]);
      sprintf(val, "%ld", a[i] + 1);
      aput(c, Str(key), Str(val));
    }
  }

  checkasync(c, 2);
  t1 = now();

  free(done);
  free(a);
  Json result = Json().set("total", (long) (n / (t1 - t0)))
    .set("puts", n)
    .set("puts_per_sec", n / (t1 - t0));
  printf("%s\n", result.unparse().c_str());
}

// update random keys from a set of 10 million.
// maybe best to run it twice, first time to
// populate the database.
void
u1(struct child *c)
{
  int i, n;
  double t0 = now();

  srandom(kvtest_first_seed + c->childno);

  for(i = 0; i < 10000000; i++){
    char key[512], val[512];
    long x = random() % 10000000;
    sprintf(key, "%ld", x);
    sprintf(val, "%ld", x + 1);
    aput(c, Str(key), Str(val));
  }
  n = i;

  checkasync(c, 2);

  double t1 = now();
  Json result = Json().set("total", (long) (n / (t1 - t0)))
    .set("puts", n)
    .set("puts_per_sec", n / (t1 - t0));
  printf("%s\n", result.unparse().c_str());
}

#define CPN 10000000

void
cpa(struct child *c)
{
  int i, n;
  double t0 = now();

  srandom(kvtest_first_seed + c->childno);

  for(i = 0; i < CPN; i++){
    char key[512], val[512];
    long x = random();
    sprintf(key, "%ld", x);
    sprintf(val, "%ld", x + 1);
    aput(c, Str(key), Str(val));
  }
  n = i;

  checkasync(c, 2);

  double t1 = now();
  Json result = Json().set("total", (long) (n / (t1 - t0)))
    .set("puts", n)
    .set("puts_per_sec", n / (t1 - t0));
  printf("%s\n", result.unparse().c_str());

}

void
cpc(struct child *c)
{
  int i, n;
  double t0 = now();

  srandom(kvtest_first_seed + c->childno);

  for(i = 0; !timeout[0]; i++){
    char key[512], val[512];
    if (i % CPN == 0)
      srandom(kvtest_first_seed + c->childno);
    long x = random();
    sprintf(key, "%ld", x);
    sprintf(val, "%ld", x + 1);
    aget(c, Str(key), Str(val), NULL);
  }
  n = i;

  checkasync(c, 2);

  double t1 = now();
  Json result = Json().set("total", (long) (n / (t1 - t0)))
    .set("gets", n)
    .set("gets_per_sec", n / (t1 - t0));
  printf("%s\n", result.unparse().c_str());
}

void
cpd(struct child *c)
{
  int i, n;
  double t0 = now();

  srandom(kvtest_first_seed + c->childno);

  for(i = 0; !timeout[0]; i++){
    char key[512], val[512];
    if (i % CPN == 0)
      srandom(kvtest_first_seed + c->childno);
    long x = random();
    sprintf(key, "%ld", x);
    sprintf(val, "%ld", x + 1);
    aput(c, Str(key), Str(val));
  }
  n = i;

  checkasync(c, 2);

  double t1 = now();
  Json result = Json().set("total", (long) (n / (t1 - t0)))
    .set("puts", n)
    .set("puts_per_sec", n / (t1 - t0));
  printf("%s\n", result.unparse().c_str());
}

// multiple threads simultaneously update the same key.
// keep track of the winning value.
// use over2 to make sure it's the same after a crash/restart.
void
over1(struct child *c)
{
  int ret, iter;

  srandom(kvtest_first_seed + c->childno);

  iter = 0;
  while(!timeout[0]){
    char key1[64], key2[64], val1[64], val2[64];
    time_t xt = time(0);
    while(xt == time(0))
      ;
    sprintf(key1, "%d", iter);
    sprintf(val1, "%ld", random());
    put(c, Str(key1), Str(val1));
    napms(500);
    ret = get(c, Str(key1), val2, sizeof(val2));
    always_assert(ret > 0);
    sprintf(key2, "%d-%d", iter, c->childno);
    put(c, Str(key2), Str(val2));
    if(c->childno == 0)
      printf("%d: %s\n", iter, val2);
    iter++;
  }
  checkasync(c, 2);
  printf("0\n");
}

// check each round of over1()
void
over2(struct child *c)
{
  int iter;

  for(iter = 0; ; iter++){
    char key1[64], key2[64], val1[64], val2[64];
    int ret;
    sprintf(key1, "%d", iter);
    ret = get(c, Str(key1), val1, sizeof(val1));
    if(ret == -1)
      break;
    sprintf(key2, "%d-%d", iter, c->childno);
    ret = get(c, Str(key2), val2, sizeof(val2));
    if(ret == -1)
      break;
    if(c->childno == 0)
      printf("%d: %s\n", iter, val2);
    always_assert(strcmp(val1, val2) == 0);
  }

  checkasync(c, 2);
  fprintf(stderr, "child %d checked %d\n", c->childno, iter);
  printf("0\n");
}

// do a bunch of inserts to distinct keys.
// rec2() checks that a prefix of those inserts are present.
// meant to be interrupted by a crash/restart.
void
rec1(struct child *c)
{
  int i;
  double t0 = now(), t1;

  srandom(kvtest_first_seed + c->childno);

  for(i = 0; !timeout[0]; i++){
    char key[512], val[512];
    long x = random();
    sprintf(key, "%ld-%d-%d", x, i, c->childno);
    sprintf(val, "%ld", x);
    aput(c, Str(key), Str(val));
  }
  checkasync(c, 2);
  t1 = now();

  fprintf(stderr, "child %d: done %d %.0f put/s\n",
          c->childno,
          i,
          i / (t1 - t0));
  printf("%.0f\n", i / (t1 - t0));
}

void
rec2(struct child *c)
{
  int i;

  srandom(kvtest_first_seed + c->childno);

  for(i = 0; ; i++){
    char key[512], val[512], wanted[512];
    long x = random();
    sprintf(key, "%ld-%d-%d", x, i, c->childno);
    sprintf(wanted, "%ld", x);
    int ret = get(c, Str(key), val, sizeof(val));
    if(ret == -1)
      break;
    val[ret] = 0;
    if(strcmp(val, wanted) != 0){
      fprintf(stderr, "oops key %s got %s wanted %s\n", key, val, wanted);
      exit(1);
    }
  }

  int i0 = i; // first missing record
  for(i = i0+1; i < i0 + 10000; i++){
    char key[512], val[512];
    long x = random();
    sprintf(key, "%ld-%d-%d", x, i, c->childno);
    val[0] = 0;
    int ret = get(c, Str(key), val, sizeof(val));
    if(ret != -1){
      printf("child %d: oops first missing %d but %d present\n",
             c->childno, i0, i);
      exit(1);
    }
  }
  checkasync(c, 2);

  fprintf(stderr, "correct prefix of %d records\n", i0);
  printf("0\n");
}

// ask server to checkpoint
void
cpb(struct child *c)
{
    if (c->childno == 0)
        c->conn->checkpoint(c->childno);
    checkasync(c, 2);
}

// mimic the first benchmark from the VoltDB blog:
//   https://voltdb.com/blog/key-value-benchmarking
//   https://voltdb.com/blog/key-value-benchmark-faq
//   http://community.voltdb.com/kvbenchdetails
//   svn checkout http://svnmirror.voltdb.com/projects/kvbench/trunk
// 500,000 items: 50-byte key, 12 KB value
// volt1a creates the DB.
// volt1b runs the benchmark.
#define VOLT1N 500000
#define VOLT1SIZE (12*1024)
void
volt1a(struct child *c)
{
  int i, j;
  double t0 = now(), t1;
  char *val = (char *) malloc(VOLT1SIZE + 1);
  always_assert(val);

  srandom(kvtest_first_seed + c->childno);

  for(i = 0; i < VOLT1SIZE; i++)
    val[i] = 'a' + (i % 26);
  val[VOLT1SIZE] = '\0';

  // XXX insert the keys in a random order to maintain
  // tree balance.
  int *keys = (int *) malloc(sizeof(int) * VOLT1N);
  always_assert(keys);
  for(i = 0; i < VOLT1N; i++)
    keys[i] = i;
  for(i = 0; i < VOLT1N; i++){
    int x = random() % VOLT1N;
    int tmp = keys[i];
    keys[i] = keys[x];
    keys[x] = tmp;
  }

  for(i = 0; i < VOLT1N; i++){
    char key[100];
    sprintf(key, "%-50d", keys[i]);
    for(j = 0; j < 20; j++)
      val[j] = 'a' + (j % 26);
    sprintf(val, ">%d", keys[i]);
    int j = strlen(val);
    val[j] = '<';
    always_assert(strlen(val) == VOLT1SIZE);
    always_assert(strlen(key) == 50);
    always_assert(isdigit(key[0]));
    aput(c, Str(key), Str(val));
  }
  checkasync(c, 2);
  t1 = now();

  free(val);
  free(keys);

  Json result = Json().set("total", (long) (i / (t1 - t0)));
  printf("%s\n", result.unparse().c_str());
}

// the actual volt1 benchmark.
// get or update with equal probability.
// their client pipelines many requests.
// they use 8 client threads.
// blog post says, for one server, VoltDB 17000, Cassandra 9740
// this benchmark ends up being network or disk limited,
// due to the huge values.
void
volt1b(struct child *c)
{
  int i, n, j;
  double t0 = now(), t1;
  char *wanted = (char *) malloc(VOLT1SIZE + 1);
  always_assert(wanted);

  for(i = 0; i < VOLT1SIZE; i++)
    wanted[i] = 'a' + (i % 26);
  wanted[VOLT1SIZE] = '\0';

  srandom(kvtest_first_seed + c->childno);

  for(i = 0; !timeout[0]; i++){
    char key[100];
    int x = random() % VOLT1N;
    sprintf(key, "%-50d", x);
    for(j = 0; j < 20; j++)
      wanted[j] = 'a' + (j % 26);
    sprintf(wanted, ">%d", x);
    int j = strlen(wanted);
    wanted[j] = '<';
    if(i > 1)
      checkasync(c, 1); // try to avoid deadlock, only 2 reqs outstanding
    if((random() % 2) == 0)
        aget(c, Str(key, 50), Str(wanted, VOLT1SIZE), 0);
    else
        aput(c, Str(key, 50), Str(wanted, VOLT1SIZE));
  }
  n = i;

  checkasync(c, 2);
  t1 = now();

  Json result = Json().set("total", (long) (n / (t1 - t0)));
  printf("%s\n", result.unparse().c_str());
}

// second VoltDB benchmark.
// 500,000 pairs, 50-byte key, value is 50 32-bit ints.
// pick a key, read one int, if odd, write a different int (same key).
// i'm simulating columns by embedding column name in key: rowname-colname.
// also the read/modify/write is not atomic.
// volt2a creates the DB.
// volt2b runs the benchmark.
#define VOLT2N 500000
#define VOLT2INTS 50
void
volt2a(struct child *c)
{
  int i, j, n = 0;
  double t0 = now(), t1;

  srandom(kvtest_first_seed + c->childno);

  // XXX insert the keys in a random order to maintain
  // tree balance.
  int *keys = (int *) malloc(sizeof(int) * VOLT2N);
  always_assert(keys);
  for(i = 0; i < VOLT2N; i++)
    keys[i] = i;
  for(i = 0; i < VOLT2N; i++){
    int x = random() % VOLT2N;
    int tmp = keys[i];
    keys[i] = keys[x];
    keys[x] = tmp;
  }

  int subkeys[VOLT2INTS];
  for(i = 0; i < VOLT2INTS; i++)
    subkeys[i] = i;
  for(i = 0; i < VOLT2INTS; i++){
    int x = random() % VOLT2INTS;
    int tmp = subkeys[i];
    subkeys[i] = subkeys[x];
    subkeys[x] = tmp;
  }

  for(i = 0; i < VOLT2N; i++){
    for(j = 0; j < VOLT2INTS; j++){
      char val[32], key[100];
      int k;
      sprintf(key, "%d-%d", keys[i], subkeys[j]);
      for(k = strlen(key); k < 50; k++)
        key[k] = ' ';
      key[50] = '\0';
      sprintf(val, "%ld", random());
      aput(c, Str(key), Str(val));
      n++;
    }
  }
  checkasync(c, 2);
  t1 = now();

  free(keys);
  Json result = Json().set("total", (long) (n / (t1 - t0)));
  printf("%s\n", result.unparse().c_str());
}

// get callback
void
volt2b1(struct child *c, struct async *a, bool, const Str &val)
{
  int k = atoi(a->key);
  int v = atoi(val.s);
  if((v % 2) == 1){
    char key[100], val[100];
    sprintf(key, "%d-%ld", k, random() % VOLT2INTS);
    for (int i = strlen(key); i < 50; i++)
      key[i] = ' ';
    sprintf(val, "%ld", random());
    aput(c, Str(key, 50), Str(val));
  }
}

void
volt2b(struct child *c)
{
  int i, n;
  double t0 = now(), t1;
  srandom(kvtest_first_seed + c->childno);
  for(i = 0; !timeout[0]; i++){
    char key[100];
    int x = random() % VOLT2N;
    int y = random() % VOLT2INTS;
    sprintf(key, "%d-%d", x, y);
    int j;
    for(j = strlen(key); j < 50; j++)
      key[j] = ' ';
    aget(c, Str(key, 50), Str(), volt2b1);
  }
  n = i;

  checkasync(c, 2);
  t1 = now();

  Json result = Json().set("total", (long) (n / (t1 - t0)));
  printf("%s\n", result.unparse().c_str());
}

using std::vector;
using std::string;

void
scantest(struct child *c)
{
  int i;

  srandom(kvtest_first_seed + c->childno);

  for(i = 100; i < 200; i++){
    char key[32], val[32];
    int kl = sprintf(key, "k%04d", i);
    sprintf(val, "v%04d", i);
    aput(c, Str(key, kl), Str(val));
  }

  checkasync(c, 2);

  for(i = 90; i < 210; i++){
    char key[32];
    sprintf(key, "k%04d", i);
    int wanted = random() % 10;
    c->conn->sendscanwhole(key, wanted, 1);
    c->conn->flush();

    {
        const Json& result = c->conn->receive();
        always_assert(result && result[0] == 1);
        int n = (result.size() - 2) / 2;
        if(i <= 200 - wanted){
            always_assert(n == wanted);
        } else if(i <= 200){
            always_assert(n == 200 - i);
        } else {
            always_assert(n == 0);
        }
        int k0 = (i < 100 ? 100 : i);
        int j, ki, off = 2;
        for(j = k0, ki = 0; j < k0 + wanted && j < 200; j++, ki++, off += 2){
            char xkey[32], xval[32];
            sprintf(xkey, "k%04d", j);
            sprintf(xval, "v%04d", j);
            if (!result[off].as_s().equals(xkey)) {
                fprintf(stderr, "Assertion failed @%d: strcmp(%s, %s) == 0\n", ki, result[off].as_s().c_str(), xkey);
                always_assert(0);
            }
            always_assert(result[off + 1].as_s().equals(xval));
        }
    }

    {
        sprintf(key, "k%04d-a", i);
        c->conn->sendscanwhole(key, 1, 1);
        c->conn->flush();

        const Json& result = c->conn->receive();
        always_assert(result && result[0] == 1);
        int n = (result.size() - 2) / 2;
        if(i >= 100 && i < 199){
            always_assert(n == 1);
            sprintf(key, "k%04d", i+1);
            always_assert(result[2].as_s().equals(key));
        }
    }
  }

  c->conn->sendscanwhole("k015", 10, 1);
  c->conn->flush();

  const Json& result = c->conn->receive();
  always_assert(result && result[0] == 1);
  int n = (result.size() - 2) / 2;
  always_assert(n == 10);
  always_assert(result[2].as_s().equals("k0150"));
  always_assert(result[3].as_s().equals("v0150"));

  fprintf(stderr, "scantest OK\n");
  printf("0\n");
}

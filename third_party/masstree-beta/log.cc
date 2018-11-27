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
#include "log.hh"
#include "kvthread.hh"
#include "kvrow.hh"
#include "file.hh"
#include "query_masstree.hh"
#include "masstree_tcursor.hh"
#include "masstree_insert.hh"
#include "masstree_remove.hh"
#include "misc.hh"
#include "msgpack.hh"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
using lcdf::String;

kvepoch_t global_log_epoch;
kvepoch_t global_wake_epoch;
struct timeval log_epoch_interval;
static struct timeval log_epoch_time;
extern Masstree::default_table* tree;
extern volatile bool recovering;

kvepoch_t rec_ckp_min_epoch;
kvepoch_t rec_ckp_max_epoch;
logreplay::info_type *rec_log_infos;
kvepoch_t rec_replay_min_epoch;
kvepoch_t rec_replay_max_epoch;
kvepoch_t rec_replay_min_quiescent_last_epoch;

struct logrec_base {
    uint32_t command_;
    uint32_t size_;

    static size_t size() {
        return sizeof(logrec_base);
    }
    static size_t store(char *buf, uint32_t command) {
        // XXX check alignment on some architectures
        logrec_base *lr = reinterpret_cast<logrec_base *>(buf);
        lr->command_ = command;
        lr->size_ = sizeof(*lr);
        return sizeof(*lr);
    }
    static bool check(const char *buf) {
        const logrec_base *lr = reinterpret_cast<const logrec_base *>(buf);
        return lr->size_ >= sizeof(*lr);
    }
    static uint32_t command(const char *buf) {
        const logrec_base *lr = reinterpret_cast<const logrec_base *>(buf);
        return lr->command_;
    }
};

struct logrec_epoch {
    uint32_t command_;
    uint32_t size_;
    kvepoch_t epoch_;

    static size_t size() {
        return sizeof(logrec_epoch);
    }
    static size_t store(char *buf, uint32_t command, kvepoch_t epoch) {
        // XXX check alignment on some architectures
        logrec_epoch *lr = reinterpret_cast<logrec_epoch *>(buf);
        lr->command_ = command;
        lr->size_ = sizeof(*lr);
        lr->epoch_ = epoch;
        return sizeof(*lr);
    }
    static bool check(const char *buf) {
        const logrec_epoch *lr = reinterpret_cast<const logrec_epoch *>(buf);
        return lr->size_ >= sizeof(*lr);
    }
};

struct logrec_kv {
    uint32_t command_;
    uint32_t size_;
    kvtimestamp_t ts_;
    uint32_t keylen_;
    char buf_[0];

    static size_t size(uint32_t keylen, uint32_t vallen) {
        return sizeof(logrec_kv) + keylen + vallen;
    }
    static size_t store(char *buf, uint32_t command,
                        Str key, Str val,
                        kvtimestamp_t ts) {
        // XXX check alignment on some architectures
        logrec_kv *lr = reinterpret_cast<logrec_kv *>(buf);
        lr->command_ = command;
        lr->size_ = sizeof(*lr) + key.len + val.len;
        lr->ts_ = ts;
        lr->keylen_ = key.len;
        memcpy(lr->buf_, key.s, key.len);
        memcpy(lr->buf_ + key.len, val.s, val.len);
        return sizeof(*lr) + key.len + val.len;
    }
    static bool check(const char *buf) {
        const logrec_kv *lr = reinterpret_cast<const logrec_kv *>(buf);
        return lr->size_ >= sizeof(*lr)
            && lr->size_ >= sizeof(*lr) + lr->keylen_;
    }
};

struct logrec_kvdelta {
    uint32_t command_;
    uint32_t size_;
    kvtimestamp_t ts_;
    kvtimestamp_t prev_ts_;
    uint32_t keylen_;
    char buf_[0];

    static size_t size(uint32_t keylen, uint32_t vallen) {
        return sizeof(logrec_kvdelta) + keylen + vallen;
    }
    static size_t store(char *buf, uint32_t command,
                        Str key, Str val,
                        kvtimestamp_t prev_ts, kvtimestamp_t ts) {
        // XXX check alignment on some architectures
        logrec_kvdelta *lr = reinterpret_cast<logrec_kvdelta *>(buf);
        lr->command_ = command;
        lr->size_ = sizeof(*lr) + key.len + val.len;
        lr->ts_ = ts;
        lr->prev_ts_ = prev_ts;
        lr->keylen_ = key.len;
        memcpy(lr->buf_, key.s, key.len);
        memcpy(lr->buf_ + key.len, val.s, val.len);
        return sizeof(*lr) + key.len + val.len;
    }
    static bool check(const char *buf) {
        const logrec_kvdelta *lr = reinterpret_cast<const logrec_kvdelta *>(buf);
        return lr->size_ >= sizeof(*lr)
            && lr->size_ >= sizeof(*lr) + lr->keylen_;
    }
};


logset* logset::make(int size) {
    static_assert(sizeof(loginfo) == 2 * CACHE_LINE_SIZE, "unexpected sizeof(loginfo)");
    assert(size > 0 && size <= 64);
    char* x = new char[sizeof(loginfo) * size + sizeof(loginfo::logset_info) + CACHE_LINE_SIZE];
    char* ls_pos = x + sizeof(loginfo::logset_info);
    uintptr_t left = reinterpret_cast<uintptr_t>(ls_pos) % CACHE_LINE_SIZE;
    if (left)
        ls_pos += CACHE_LINE_SIZE - left;
    logset* ls = reinterpret_cast<logset*>(ls_pos);
    ls->li_[-1].lsi_.size_ = size;
    ls->li_[-1].lsi_.allocation_offset_ = (int) (x - ls_pos);
    for (int i = 0; i != size; ++i)
        new((void*) &ls->li_[i]) loginfo(ls, i);
    return ls;
}

void logset::free(logset* ls) {
    for (int i = 0; i != ls->size(); ++i)
        ls->li_[i].~loginfo();
    delete[] (reinterpret_cast<char*>(ls) + ls->li_[-1].lsi_.allocation_offset_);
}


loginfo::loginfo(logset* ls, int logindex) {
    f_.lock_ = 0;
    f_.waiting_ = 0;
    f_.filename_ = String().internal_rep();
    f_.filename_.ref();

    len_ = 20 * 1024 * 1024;
    pos_ = 0;
    buf_ = (char *) malloc(len_);
    always_assert(buf_);
    log_epoch_ = 0;
    quiescent_epoch_ = 0;
    wake_epoch_ = 0;
    flushed_epoch_ = 0;

    ti_ = 0;
    f_.logset_ = ls;
    logindex_ = logindex;

    (void) padding1_;
}

loginfo::~loginfo() {
    f_.filename_.deref();
    free(buf_);
}

void* loginfo::trampoline(void* x) {
    loginfo* li = reinterpret_cast<loginfo*>(x);
    li->ti_->pthread() = pthread_self();
    return li->run();
}

void loginfo::initialize(const String& logfile) {
    assert(!ti_);

    f_.filename_.deref();
    f_.filename_ = logfile.internal_rep();
    f_.filename_.ref();

    ti_ = threadinfo::make(threadinfo::TI_LOG, logindex_);
    int r = pthread_create(&ti_->pthread(), 0, trampoline, this);
    always_assert(r == 0);
}

// one logger thread per logs[].
static void check_epoch() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    if (timercmp(&tv, &log_epoch_time, >)) {
        log_epoch_time = tv;
        timeradd(&log_epoch_time, &log_epoch_interval, &log_epoch_time);
        global_log_epoch = global_log_epoch.next_nonzero(); // 0 isn't valid
    }
}

void* loginfo::run() {
    {
        logreplay replayer(f_.filename_);
        replayer.replay(ti_->index(), ti_);
    }

    int fd = open(String(f_.filename_).c_str(),
                  O_WRONLY | O_APPEND | O_CREAT, 0666);
    always_assert(fd >= 0);
    char *x_buf = (char *) malloc(len_);
    always_assert(x_buf);

    while (1) {
        uint32_t nb = 0;
        acquire();
        kvepoch_t ge = global_log_epoch, we = global_wake_epoch;
        if (wake_epoch_ != we) {
            wake_epoch_ = we;
            quiescent_epoch_ = 0;
        }
        // If the writing threads appear quiescent, and aren't about to write
        // to the log (f_.waiting_ != 0), then write a quiescence
        // notification.
        if (!recovering && pos_ == 0 && !quiescent_epoch_
            && ge != log_epoch_ && ge != we && !f_.waiting_) {
            quiescent_epoch_ = log_epoch_ = ge;
            char *p = buf_;
            p += logrec_epoch::store(p, logcmd_epoch, log_epoch_);
            if (log_epoch_ == wake_epoch_)
                p += logrec_base::store(p, logcmd_wake);
            p += logrec_base::store(p, logcmd_quiesce);
            pos_ = p - buf_;
        }
        if (!recovering && pos_ > 0) {
            uint32_t x_pos = pos_;
            std::swap(buf_, x_buf);
            pos_ = 0;
            kvepoch_t x_epoch = log_epoch_;
            release();
            ssize_t r = write(fd, x_buf, x_pos);
            always_assert(r == ssize_t(x_pos));
            fsync(fd);
            flushed_epoch_ = x_epoch;
            // printf("log %d %d\n", ti_->index(), x_pos);
            nb = x_pos;
        } else
            release();
        if (nb < len_ / 4)
            napms(200);
        if (ti_->index() == 0)
            check_epoch();
    }

    return 0;
}



// log entry format: see log.hh
void loginfo::record(int command, const query_times& qtimes,
                     Str key, Str value) {
    assert(!recovering);
    size_t n = logrec_kvdelta::size(key.len, value.len)
        + logrec_epoch::size() + logrec_base::size();
    waitlist wait = { &wait };
    int stalls = 0;
    while (1) {
        if (len_ - pos_ >= n
            && (wait.next == &wait || f_.waiting_ == &wait)) {
            kvepoch_t we = global_wake_epoch;

            // Potentially record a new epoch.
            if (qtimes.epoch != log_epoch_) {
                log_epoch_ = qtimes.epoch;
                pos_ += logrec_epoch::store(buf_ + pos_, logcmd_epoch, qtimes.epoch);
            }

            if (quiescent_epoch_) {
                // We're recording a new log record on a log that's been
                // quiescent for a while. If the quiescence marker has been
                // flushed, then all epochs less than the query epoch are
                // effectively on disk.
                if (flushed_epoch_ == quiescent_epoch_)
                    flushed_epoch_ = qtimes.epoch;
                quiescent_epoch_ = 0;
                while (we < qtimes.epoch)
                    we = cmpxchg(&global_wake_epoch, we, qtimes.epoch);
            }

            // Log epochs should be recorded in monotonically increasing
            // order, but the wake epoch may be ahead of the query epoch (if
            // the query took a while). So potentially record an EARLIER
            // wake_epoch. This will get fixed shortly by the next log
            // record.
            if (we != wake_epoch_ && qtimes.epoch < we)
                we = qtimes.epoch;
            if (we != wake_epoch_) {
                wake_epoch_ = we;
                pos_ += logrec_base::store(buf_ + pos_, logcmd_wake);
            }

            if (command == logcmd_put && qtimes.prev_ts
                && !(qtimes.prev_ts & 1))
                pos_ += logrec_kvdelta::store(buf_ + pos_,
                                              logcmd_modify, key, value,
                                              qtimes.prev_ts, qtimes.ts);
            else
                pos_ += logrec_kv::store(buf_ + pos_,
                                         command, key, value, qtimes.ts);

            if (f_.waiting_ == &wait)
                f_.waiting_ = wait.next;
            release();
            return;
        }

        // Otherwise must spin
        if (wait.next == &wait) {
            waitlist** p = &f_.waiting_;
            while (*p)
                p = &(*p)->next;
            *p = &wait;
            wait.next = 0;
        }
        release();
        if (stalls == 0)
            printf("stall\n");
        else if (stalls % 25 == 0)
            printf("stall %d\n", stalls);
        ++stalls;
        napms(50);
        acquire();
    }
}

void loginfo::record(int command, const query_times& qtimes, Str key,
                     const lcdf::Json* req, const lcdf::Json* end_req) {
    lcdf::StringAccum sa(128);
    msgpack::unparser<lcdf::StringAccum> cu(sa);
    cu.write_array_header(end_req - req);
    for (; req != end_req; ++req)
        cu << *req;
    record(command, qtimes, key, Str(sa.data(), sa.length()));
}


// replay

logreplay::logreplay(const String &filename)
    : filename_(filename), errno_(0), buf_()
{
    int fd = open(filename_.c_str(), O_RDONLY);
    if (fd == -1) {
    fail:
        errno_ = errno;
        buf_ = 0;
        if (fd != -1)
            (void) close(fd);
        return;
    }

    struct stat sb;
    int r = fstat(fd, &sb);
    if (r == -1)
        goto fail;

    size_ = sb.st_size;
    if (size_ != 0) {
        // XXX what if filename_ is too big to mmap in its entirety?
        // XXX should support mmaping/writing in pieces
        buf_ = (char *) ::mmap(0, size_, PROT_READ, MAP_FILE | MAP_PRIVATE,
                               fd, 0);
        if (buf_ == MAP_FAILED)
            goto fail;
    }

    (void) close(fd);
}

logreplay::~logreplay()
{
    unmap();
}

int
logreplay::unmap()
{
    int r = 0;
    if (buf_) {
        r = munmap(buf_, size_);
        buf_ = 0;
    }
    return r;
}


struct logrecord {
    uint32_t command;
    Str key;
    Str val;
    kvtimestamp_t ts;
    kvtimestamp_t prev_ts;
    kvepoch_t epoch;

    const char *extract(const char *buf, const char *end);

    template <typename T>
    void run(T& table, std::vector<lcdf::Json>& jrepo, threadinfo& ti);

  private:
    inline void apply(row_type*& value, bool found,
                      std::vector<lcdf::Json>& jrepo, threadinfo& ti);
};

const char *
logrecord::extract(const char *buf, const char *end)
{
    const logrec_base *lr = reinterpret_cast<const logrec_base *>(buf);
    if (unlikely(size_t(end - buf) < sizeof(*lr)
                 || lr->size_ < sizeof(*lr)
                 || size_t(end - buf) < lr->size_
                 || lr->command_ == logcmd_none)) {
    fail:
        command = logcmd_none;
        return end;
    }

    command = lr->command_;
    if (command == logcmd_put || command == logcmd_replace
        || command == logcmd_remove) {
        const logrec_kv *lk = reinterpret_cast<const logrec_kv *>(buf);
        if (unlikely(lk->size_ < sizeof(*lk)
                     || lk->keylen_ > MASSTREE_MAXKEYLEN
                     || sizeof(*lk) + lk->keylen_ > lk->size_))
            goto fail;
        ts = lk->ts_;
        key.assign(lk->buf_, lk->keylen_);
        val.assign(lk->buf_ + lk->keylen_, lk->size_ - sizeof(*lk) - lk->keylen_);
    } else if (command == logcmd_modify) {
        const logrec_kvdelta *lk = reinterpret_cast<const logrec_kvdelta *>(buf);
        if (unlikely(lk->keylen_ > MASSTREE_MAXKEYLEN
                     || sizeof(*lk) + lk->keylen_ > lk->size_))
            goto fail;
        ts = lk->ts_;
        prev_ts = lk->prev_ts_;
        key.assign(lk->buf_, lk->keylen_);
        val.assign(lk->buf_ + lk->keylen_, lk->size_ - sizeof(*lk) - lk->keylen_);
    } else if (command == logcmd_epoch) {
        const logrec_epoch *lre = reinterpret_cast<const logrec_epoch *>(buf);
        if (unlikely(lre->size_ < logrec_epoch::size()))
            goto fail;
        epoch = lre->epoch_;
    }

    return buf + lr->size_;
}

template <typename T>
void logrecord::run(T& table, std::vector<lcdf::Json>& jrepo, threadinfo& ti) {
    row_marker m;
    if (command == logcmd_remove) {
        ts |= 1;
        m.marker_type_ = row_marker::mt_remove;
        val = Str((const char*) &m, sizeof(m));
    }

    typename T::cursor_type lp(table, key);
    bool found = lp.find_insert(ti);
    if (!found)
        ti.observe_phantoms(lp.node());
    apply(lp.value(), found, jrepo, ti);
    lp.finish(1, ti);
}

static lcdf::Json* parse_changeset(Str changeset,
                                   std::vector<lcdf::Json>& jrepo) {
    msgpack::parser mp(changeset.udata());
    unsigned index = 0;
    Str value;
    size_t pos = 0;
    while (mp.position() != changeset.end()) {
        if (pos == jrepo.size())
            jrepo.resize(pos + 2);
        mp >> index >> value;
        jrepo[pos] = index;
        jrepo[pos + 1] = String::make_stable(value);
        pos += 2;
    }
    return jrepo.data() + pos;
}

inline void logrecord::apply(row_type*& value, bool found,
                             std::vector<lcdf::Json>& jrepo, threadinfo& ti) {
    row_type** cur_value = &value;
    if (!found)
        *cur_value = 0;

    // find point to insert change (may be after some delta markers)
    while (*cur_value && row_is_delta_marker(*cur_value)
           && (*cur_value)->timestamp() > ts)
        cur_value = &row_get_delta_marker(*cur_value)->prev_;

    // check out of date
    if (*cur_value && (*cur_value)->timestamp() >= ts)
        return;

    // if not modifying, delete everything earlier
    if (command != logcmd_modify)
        while (row_type* old_value = *cur_value) {
            if (row_is_delta_marker(old_value)) {
                ti.mark(tc_replay_remove_delta);
                *cur_value = row_get_delta_marker(old_value)->prev_;
            } else
                *cur_value = 0;
            old_value->deallocate(ti);
        }

    // actually apply change
    if (command == logcmd_replace)
        *cur_value = row_type::create1(val, ts, ti);
    else if (command != logcmd_modify
             || (*cur_value && (*cur_value)->timestamp() == prev_ts)) {
        lcdf::Json* end_req = parse_changeset(val, jrepo);
        if (command != logcmd_modify)
            *cur_value = row_type::create(jrepo.data(), end_req, ts, ti);
        else {
            row_type* old_value = *cur_value;
            *cur_value = old_value->update(jrepo.data(), end_req, ts, ti);
            if (*cur_value != old_value)
                old_value->deallocate(ti);
        }
    } else {
        // XXX assume that memory exists before saved request -- it does
        // in conventional log replay, but that's an ugly interface
        val.s -= sizeof(row_delta_marker<row_type>);
        val.len += sizeof(row_delta_marker<row_type>);
        row_type* new_value = row_type::create1(val, ts | 1, ti);
        row_delta_marker<row_type>* dm = row_get_delta_marker(new_value, true);
        dm->marker_type_ = row_marker::mt_delta;
        dm->prev_ts_ = prev_ts;
        dm->prev_ = *cur_value;
        *cur_value = new_value;
        ti.mark(tc_replay_create_delta);
    }

    // clean up
    while (value && row_is_delta_marker(value)) {
        row_type **prev = 0, **trav = &value;
        while (*trav && row_is_delta_marker(*trav)) {
            prev = trav;
            trav = &row_get_delta_marker(*trav)->prev_;
        }
        if (prev && *trav
            && row_get_delta_marker(*prev)->prev_ts_ == (*trav)->timestamp()) {
            row_type *old_prev = *prev;
            Str req = old_prev->col(0);
            req.s += sizeof(row_delta_marker<row_type>);
            req.len -= sizeof(row_delta_marker<row_type>);
            const lcdf::Json* end_req = parse_changeset(req, jrepo);
            *prev = (*trav)->update(jrepo.data(), end_req, old_prev->timestamp() - 1, ti);
            if (*prev != *trav)
                (*trav)->deallocate(ti);
            old_prev->deallocate(ti);
            ti.mark(tc_replay_remove_delta);
        } else
            break;
    }
}


logreplay::info_type
logreplay::info() const
{
    info_type x;
    x.first_epoch = x.last_epoch = x.wake_epoch = x.min_post_quiescent_wake_epoch = 0;
    x.quiescent = true;

    const char *buf = buf_, *end = buf_ + size_;
    off_t nr = 0;
    bool log_corrupt = false;
    while (buf + sizeof(logrec_base) <= end) {
        const logrec_base *lr = reinterpret_cast<const logrec_base *>(buf);
        if (unlikely(lr->size_ < sizeof(logrec_base))) {
            log_corrupt = true;
            break;
        } else if (unlikely(buf + lr->size_ > end))
            break;
        x.quiescent = lr->command_ == logcmd_quiesce;
        if (lr->command_ == logcmd_epoch) {
            const logrec_epoch *lre =
                reinterpret_cast<const logrec_epoch *>(buf);
            if (unlikely(lre->size_ < sizeof(*lre))) {
                log_corrupt = true;
                break;
            }
            if (!x.first_epoch)
                x.first_epoch = lre->epoch_;
            x.last_epoch = lre->epoch_;
            if (x.wake_epoch && x.wake_epoch > x.last_epoch) // wrap-around
                x.wake_epoch = 0;
        } else if (lr->command_ == logcmd_wake)
            x.wake_epoch = x.last_epoch;
#if !NDEBUG
        else if (lr->command_ != logcmd_put
                 && lr->command_ != logcmd_replace
                 && lr->command_ != logcmd_modify
                 && lr->command_ != logcmd_remove
                 && lr->command_ != logcmd_quiesce) {
            log_corrupt = true;
            break;
        }
#endif
        buf += lr->size_;
        ++nr;
    }

    fprintf(stderr, "replay %s: %" PRIdOFF_T " records, first %" PRIu64 ", last %" PRIu64 ", wake %" PRIu64 "%s%s @%zu\n",
            filename_.c_str(), nr, x.first_epoch.value(),
            x.last_epoch.value(), x.wake_epoch.value(),
            x.quiescent ? ", quiescent" : "",
            log_corrupt ? ", CORRUPT" : "", buf - buf_);
    return x;
}

kvepoch_t
logreplay::min_post_quiescent_wake_epoch(kvepoch_t quiescent_epoch) const
{
    kvepoch_t e = 0;
    const char *buf = buf_, *end = buf_ + size_;
    bool log_corrupt = false;
    while (buf + sizeof(logrec_base) <= end) {
        const logrec_base *lr = reinterpret_cast<const logrec_base *>(buf);
        if (unlikely(lr->size_ < sizeof(logrec_base))) {
            log_corrupt = true;
            break;
        } else if (unlikely(buf + lr->size_ > end))
            break;
        if (lr->command_ == logcmd_epoch) {
            const logrec_epoch *lre =
                reinterpret_cast<const logrec_epoch *>(buf);
            if (unlikely(lre->size_ < sizeof(*lre))) {
                log_corrupt = true;
                break;
            }
            e = lre->epoch_;
        } else if (lr->command_ == logcmd_wake
                   && e
                   && e >= quiescent_epoch)
            return e;
        buf += lr->size_;
    }
    (void) log_corrupt;
    return 0;
}

uint64_t
logreplay::replayandclean1(kvepoch_t min_epoch, kvepoch_t max_epoch,
                           threadinfo *ti)
{
    uint64_t nr = 0;
    const char *pos = buf_, *end = buf_ + size_;
    const char *repbegin = 0, *repend = 0;
    logrecord lr;
    std::vector<lcdf::Json> jrepo;

    // XXX
    while (pos < end) {
        const char *nextpos = lr.extract(pos, end);
        if (lr.command == logcmd_none) {
            fprintf(stderr, "replay %s: %" PRIu64 " entries replayed, CORRUPT @%zu\n",
                    filename_.c_str(), nr, pos - buf_);
            break;
        }
        if (lr.command == logcmd_epoch) {
            if ((min_epoch && lr.epoch < min_epoch)
                || (!min_epoch && !repbegin))
                repbegin = pos;
            if (lr.epoch >= max_epoch) {
                always_assert(repbegin);
                repend = nextpos;
                break;
            }
        }
        if (!lr.epoch || (min_epoch && lr.epoch < min_epoch)) {
            pos = nextpos;
            if (repbegin)
                repend = nextpos;
            continue;
        }
        // replay only part of log after checkpoint
        // could replay everything, the if() here tests
        // correctness of checkpoint scheme.
        assert(repbegin);
        repend = nextpos;
        if (lr.key.len) { // skip empty entry
            if (lr.command == logcmd_put
                || lr.command == logcmd_replace
                || lr.command == logcmd_modify
                || lr.command == logcmd_remove)
                lr.run(tree->table(), jrepo, *ti);
            ++nr;
            if (nr % 100000 == 0)
                fprintf(stderr,
                        "replay %s: %" PRIu64 " entries replayed\n",
                        filename_.c_str(), nr);
        }
        // XXX RCU
        pos = nextpos;
    }

    // rewrite portion of log
    if (!repbegin)
        repbegin = repend = buf_;
    else if (!repend) {
        fprintf(stderr, "replay %s: surprise repend\n", filename_.c_str());
        repend = pos;
    }

    char tmplog[256];
    int r = snprintf(tmplog, sizeof(tmplog), "%s.tmp", filename_.c_str());
    always_assert(r >= 0 && size_t(r) < sizeof(tmplog));

    printf("replay %s: truncate from %" PRIdOFF_T " to %" PRIdSIZE_T " [%" PRIdSIZE_T ",%" PRIdSIZE_T ")\n",
           filename_.c_str(), size_, repend - repbegin,
           repbegin - buf_, repend - buf_);

    bool need_copy = repbegin != buf_;
    int fd;
    if (!need_copy)
        fd = replay_truncate(repend - repbegin);
    else
        fd = replay_copy(tmplog, repbegin, repend);

    r = fsync(fd);
    always_assert(r == 0);
    r = close(fd);
    always_assert(r == 0);

    // replace old log with rewritten log
    if (unmap() != 0)
        abort();

    if (need_copy) {
        r = rename(tmplog, filename_.c_str());
        if (r != 0) {
            fprintf(stderr, "replay %s: %s\n", filename_.c_str(), strerror(errno));
            abort();
        }
    }

    return nr;
}

int
logreplay::replay_truncate(size_t len)
{
    int fd = open(filename_.c_str(), O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "replay %s: %s\n", filename_.c_str(), strerror(errno));
        abort();
    }

    struct stat sb;
    int r = fstat(fd, &sb);
    if (r != 0) {
        fprintf(stderr, "replay %s: %s\n", filename_.c_str(), strerror(errno));
        abort();
    } else if (sb.st_size < off_t(len)) {
        fprintf(stderr, "replay %s: bad length %" PRIdOFF_T "\n", filename_.c_str(), sb.st_size);
        abort();
    }

    r = ftruncate(fd, len);
    if (r != 0) {
        fprintf(stderr, "replay %s: truncate: %s\n", filename_.c_str(), strerror(errno));
        abort();
    }

    off_t off = lseek(fd, len, SEEK_SET);
    if (off == (off_t) -1) {
        fprintf(stderr, "replay %s: seek: %s\n", filename_.c_str(), strerror(errno));
        abort();
    }

    return fd;
}

int
logreplay::replay_copy(const char *tmpname, const char *first, const char *last)
{
    int fd = creat(tmpname, 0666);
    if (fd < 0) {
        fprintf(stderr, "replay %s: create: %s\n", tmpname, strerror(errno));
        abort();
    }

    ssize_t w = safe_write(fd, first, last - first);
    always_assert(w >= 0 && w == last - first);

    return fd;
}

void
logreplay::replay(int which, threadinfo *ti)
{
    waituntilphase(REC_LOG_TS);
    // find the maximum timestamp of entries in the log
    if (buf_) {
        info_type x = info();
        pthread_mutex_lock(&rec_mu);
        rec_log_infos[which] = x;
        pthread_mutex_unlock(&rec_mu);
    }
    inactive();

    waituntilphase(REC_LOG_ANALYZE_WAKE);
    if (buf_) {
        if (rec_replay_min_quiescent_last_epoch
            && rec_replay_min_quiescent_last_epoch <= rec_log_infos[which].wake_epoch)
            rec_log_infos[which].min_post_quiescent_wake_epoch =
                min_post_quiescent_wake_epoch(rec_replay_min_quiescent_last_epoch);
    }
    inactive();

    waituntilphase(REC_LOG_REPLAY);
    if (buf_) {
        ti->rcu_start();
        uint64_t nr = replayandclean1(rec_replay_min_epoch, rec_replay_max_epoch, ti);
        ti->rcu_stop();
        printf("recovered %" PRIu64 " records from %s\n", nr, filename_.c_str());
    }
    inactive();
}

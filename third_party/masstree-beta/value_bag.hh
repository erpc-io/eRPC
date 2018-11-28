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
#ifndef VALUE_BAG_HH
#define VALUE_BAG_HH
#include "kvthread.hh"
#include "json.hh"

template <typename O>
class value_bag {
  public:
    typedef short index_type;
    typedef O offset_type;
    typedef lcdf::Str Str;
    typedef lcdf::Json Json;

  private:
    union bagdata {
        struct {
            offset_type ncol_;
            offset_type pos_[1];
        };
        char s_[0];
    };

  public:
    static const char *name() { return "Bag"; }

    inline value_bag();

    inline kvtimestamp_t timestamp() const;
    inline size_t size() const;
    inline int ncol() const;
    inline O column_length(int i) const;
    inline Str col(int i) const;

    inline Str row_string() const;

    template <typename ALLOC> inline void deallocate(ALLOC& ti);
    template <typename ALLOC> inline void deallocate_rcu(ALLOC& ti);

    template <typename ALLOC>
    value_bag<O>* update(const Json* first, const Json* last, kvtimestamp_t ts,
                         ALLOC& ti) const;
    template <typename ALLOC>
    inline value_bag<O>* update(int col, Str value,
                                kvtimestamp_t ts, ALLOC& ti) const;
    template <typename ALLOC>
    static value_bag<O>* create(const Json* first, const Json* last,
                                kvtimestamp_t ts, ALLOC& ti);
    template <typename ALLOC>
    static value_bag<O>* create1(Str value, kvtimestamp_t ts, ALLOC& ti);
    template <typename ALLOC>
    inline void deallocate_rcu_after_update(const Json* first, const Json* last, ALLOC& ti);
    template <typename ALLOC>
    inline void deallocate_after_failed_update(const Json* first, const Json* last, ALLOC& ti);

    template <typename PARSER, typename ALLOC>
    static value_bag<O>* checkpoint_read(PARSER& par, kvtimestamp_t ts,
                                         ALLOC& ti);
    template <typename UNPARSER>
    inline void checkpoint_write(UNPARSER& unpar) const;

    void print(FILE* f, const char* prefix, int indent, Str key,
               kvtimestamp_t initial_ts, const char* suffix = "");

  private:
    kvtimestamp_t ts_;
    bagdata d_;
};


template <typename O>
inline value_bag<O>::value_bag()
    : ts_(0) {
    d_.ncol_ = 0;
    d_.pos_[0] = sizeof(bagdata);
}

template <typename O>
inline kvtimestamp_t value_bag<O>::timestamp() const {
    return ts_;
}

template <typename O>
inline size_t value_bag<O>::size() const {
    return sizeof(kvtimestamp_t) + d_.pos_[d_.ncol_];
}

template <typename O>
inline int value_bag<O>::ncol() const {
    return d_.ncol_;
}

template <typename O>
inline O value_bag<O>::column_length(int i) const {
    return d_.pos_[i + 1] - d_.pos_[i];
}

template <typename O>
inline lcdf::Str value_bag<O>::col(int i) const {
    if (unsigned(i) < unsigned(d_.ncol_))
        return Str(d_.s_ + d_.pos_[i], column_length(i));
    else
        return Str();
}

template <typename O>
inline lcdf::Str value_bag<O>::row_string() const {
    return Str(d_.s_, d_.pos_[d_.ncol_]);
}

template <typename O> template <typename ALLOC>
inline void value_bag<O>::deallocate(ALLOC& ti) {
    ti.deallocate(this, size(), memtag_value);
}

template <typename O> template <typename ALLOC>
inline void value_bag<O>::deallocate_rcu(ALLOC& ti) {
    ti.deallocate_rcu(this, size(), memtag_value);
}

// prerequisite: [first, last) is an array [column, value, column, value, ...]
// each column is unsigned; the columns are strictly increasing;
// each value is a string
template <typename O> template <typename ALLOC>
value_bag<O>* value_bag<O>::update(const Json* first, const Json* last,
                                   kvtimestamp_t ts, ALLOC& ti) const
{
    size_t sz = size();
    unsigned ncol = d_.ncol_;
    for (auto it = first; it != last; it += 2) {
        unsigned idx = it[0].as_u();
        sz += it[1].as_s().length();
        if (idx < d_.ncol_)
            sz -= column_length(idx);
        else
            ncol = idx + 1;
    }
    if (ncol > d_.ncol_)
        sz += (ncol - d_.ncol_) * sizeof(offset_type);

    value_bag<O>* row = (value_bag<O>*) ti.allocate(sz, memtag_value);
    row->ts_ = ts;

    // Minor optimization: Replacing one small column without changing length
    if (ncol == d_.ncol_ && sz == size() && first + 2 == last
        && first[1].as_s().length() <= 16) {
        memcpy(row->d_.s_, d_.s_, sz - sizeof(kvtimestamp_t));
        memcpy(row->d_.s_ + d_.pos_[first[0].as_u()],
               first[1].as_s().data(), first[1].as_s().length());
        return row;
    }

    // Otherwise need to do more work
    row->d_.ncol_ = ncol;
    sz = sizeof(bagdata) + ncol * sizeof(offset_type);
    unsigned col = 0;
    while (1) {
        unsigned this_col = (first != last ? first[0].as_u() : ncol);

        // copy data from old row
        if (col != this_col && col < d_.ncol_) {
            unsigned end_col = std::min(this_col, unsigned(d_.ncol_));
            ssize_t delta = sz - d_.pos_[col];
            if (delta == 0)
                memcpy(row->d_.pos_ + col, d_.pos_ + col,
                       sizeof(offset_type) * (end_col - col));
            else
                for (unsigned i = col; i < end_col; ++i)
                    row->d_.pos_[i] = d_.pos_[i] + delta;
            size_t amt = d_.pos_[end_col] - d_.pos_[col];
            memcpy(row->d_.s_ + sz, d_.s_ + d_.pos_[col], amt);
            sz += amt;
            col = end_col;
        }

        // mark empty columns if we're extending
        while (col != this_col) {
            row->d_.pos_[col] = sz;
            ++col;
        }

        if (col == ncol)
            break;

        // copy data from change
        row->d_.pos_[col] = sz;
        Str val = first[1].as_s();
        memcpy(row->d_.s_ + sz, val.data(), val.length());
        sz += val.length();
        first += 2;
        ++col;
    }
    row->d_.pos_[ncol] = sz;
    return row;
}

template <typename O> template <typename ALLOC>
inline value_bag<O>* value_bag<O>::update(int col, Str value, kvtimestamp_t ts,
                                          ALLOC& ti) const {
    Json change[2] = {Json(col), Json(value)};
    return update(&change[0], &change[2], ts, ti);
}

template <typename O> template <typename ALLOC>
inline value_bag<O>* value_bag<O>::create(const Json* first, const Json* last,
                                          kvtimestamp_t ts, ALLOC& ti) {
    value_bag<O> empty;
    return empty.update(first, last, ts, ti);
}

template <typename O> template <typename ALLOC>
inline value_bag<O>* value_bag<O>::create1(Str str, kvtimestamp_t ts,
                                           ALLOC& ti) {
    value_bag<O>* row = (value_bag<O>*) ti.allocate(sizeof(kvtimestamp_t) + sizeof(bagdata) + sizeof(O) + str.length(), memtag_value);
    row->ts_ = ts;
    row->d_.ncol_ = 1;
    row->d_.pos_[0] = sizeof(bagdata) + sizeof(O);
    row->d_.pos_[1] = sizeof(bagdata) + sizeof(O) + str.length();
    memcpy(row->d_.s_ + row->d_.pos_[0], str.data(), str.length());
    return row;
}

template <typename O> template <typename ALLOC>
inline void value_bag<O>::deallocate_rcu_after_update(const Json*, const Json*, ALLOC& ti) {
    deallocate_rcu(ti);
}

template <typename O> template <typename ALLOC>
inline void value_bag<O>::deallocate_after_failed_update(const Json*, const Json*, ALLOC& ti) {
    deallocate(ti);
}

template <typename O> template <typename PARSER, typename ALLOC>
inline value_bag<O>* value_bag<O>::checkpoint_read(PARSER& par,
                                                   kvtimestamp_t ts,
                                                   ALLOC& ti) {
    Str value;
    par >> value;
    value_bag<O>* row = (value_bag<O>*) ti.allocate(sizeof(kvtimestamp_t) + value.length(), memtag_value);
    row->ts_ = ts;
    memcpy(row->d_.s_, value.data(), value.length());
    return row;
}

template <typename O> template <typename UNPARSER>
inline void value_bag<O>::checkpoint_write(UNPARSER& unpar) const {
    unpar << Str(d_.s_, d_.pos_[d_.ncol_]);
}

template <typename O>
void value_bag<O>::print(FILE *f, const char *prefix, int indent,
                         Str key, kvtimestamp_t initial_ts,
                         const char *suffix)
{
    kvtimestamp_t adj_ts = timestamp_sub(ts_, initial_ts);
    if (d_.ncol_ == 1)
        fprintf(f, "%s%*s%.*s = %.*s @" PRIKVTSPARTS "%s\n", prefix, indent, "",
                key.len, key.s, d_.pos_[1] - d_.pos_[0], d_.s_ + d_.pos_[0],
                KVTS_HIGHPART(adj_ts), KVTS_LOWPART(adj_ts), suffix);
    else {
        fprintf(f, "%s%*s%.*s = [", prefix, indent, "", key.len, key.s);
        for (int col = 0; col < d_.ncol_; ++col) {
            int pos = d_.pos_[col], len = std::min(40, d_.pos_[col + 1] - pos);
            fprintf(f, col ? "|%.*s" : "%.*s", len, d_.s_ + pos);
        }
        fprintf(f, "] @" PRIKVTSPARTS "%s\n",
                KVTS_HIGHPART(adj_ts), KVTS_LOWPART(adj_ts), suffix);
    }
}

#endif

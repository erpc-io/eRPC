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
#ifndef VALUE_STRING_HH
#define VALUE_STRING_HH
#include "compiler.hh"
#include "json.hh"

class value_string {
  public:
    typedef unsigned index_type;
    static const char *name() { return "String"; }

    typedef lcdf::Str Str;
    typedef lcdf::Json Json;

    inline value_string();

    inline kvtimestamp_t timestamp() const;
    inline size_t size() const;
    inline int ncol() const;
    inline Str col(index_type idx) const;

    template <typename ALLOC>
    inline void deallocate(ALLOC& ti);
    inline void deallocate_rcu(threadinfo& ti);

    template <typename ALLOC>
    value_string* update(const Json* first, const Json* last, kvtimestamp_t ts, ALLOC& ti) const;
    static inline value_string* create(const Json* first, const Json* last, kvtimestamp_t ts, threadinfo& ti);
    static inline value_string* create1(Str value, kvtimestamp_t ts, threadinfo& ti);
    inline void deallocate_rcu_after_update(const Json* first, const Json* last, threadinfo& ti);
    inline void deallocate_after_failed_update(const Json* first, const Json* last, threadinfo& ti);

    template <typename PARSER>
    static inline value_string* checkpoint_read(PARSER& par, kvtimestamp_t ts,
                                                threadinfo& ti);
    template <typename UNPARSER>
    inline void checkpoint_write(UNPARSER& unpar) const;

    void print(FILE* f, const char* prefix, int indent, Str key,
               kvtimestamp_t initial_ts, const char* suffix = "") {
        kvtimestamp_t adj_ts = timestamp_sub(ts_, initial_ts);
        fprintf(f, "%s%*s%.*s = %.*s @" PRIKVTSPARTS "%s\n", prefix, indent, "",
                key.len, key.s, std::min(40U, vallen_), s_,
                KVTS_HIGHPART(adj_ts), KVTS_LOWPART(adj_ts), suffix);
    }

    static inline index_type make_index(unsigned offset, unsigned length);
    static inline unsigned index_offset(index_type idx);
    static inline unsigned index_length(index_type idx);

  private:
    kvtimestamp_t ts_;
    unsigned vallen_;
    char s_[0];

    static inline unsigned index_last_offset(index_type idx);
    static inline size_t shallow_size(int vallen);
    inline size_t shallow_size() const;
};

inline value_string::index_type value_string::make_index(unsigned offset, unsigned length) {
    return offset + (length << 16);
}

inline unsigned value_string::index_offset(index_type idx) {
    return idx & 0xFFFF;
}

inline unsigned value_string::index_length(index_type idx) {
    return idx >> 16;
}

inline value_string::value_string()
    : ts_(0), vallen_(0) {
}

inline kvtimestamp_t value_string::timestamp() const {
    return ts_;
}

inline size_t value_string::size() const {
    return sizeof(value_string) + vallen_;
}

inline int value_string::ncol() const {
    return 1;
}

inline unsigned value_string::index_last_offset(index_type idx) {
    return index_offset(idx) + index_length(idx);
}

inline lcdf::Str value_string::col(index_type idx) const {
    if (idx == 0)
        return Str(s_, vallen_);
    else {
        unsigned off = std::min(vallen_, index_offset(idx));
        return Str(s_ + off, std::min(vallen_ - off, index_length(idx)));
    }
}

template <typename ALLOC>
inline void value_string::deallocate(ALLOC& ti) {
    ti.deallocate(this, size(), memtag_value);
}

inline void value_string::deallocate_rcu(threadinfo& ti) {
    ti.deallocate_rcu(this, size(), memtag_value);
}

inline size_t value_string::shallow_size(int vallen) {
    return sizeof(value_string) + vallen;
}

inline size_t value_string::shallow_size() const {
    return shallow_size(vallen_);
}

template <typename ALLOC>
value_string* value_string::update(const Json* first, const Json* last,
                                   kvtimestamp_t ts, ALLOC& ti) const {
    unsigned vallen = 0, cut = vallen_;
    for (auto it = first; it != last; it += 2) {
        unsigned idx = it[0].as_u(), length = it[1].as_s().length();
        if (idx == 0)
            cut = length;
        vallen = std::max(vallen, index_offset(idx) + length);
    }
    vallen = std::max(vallen, cut);
    value_string* row = (value_string*) ti.allocate(shallow_size(vallen), memtag_value);
    row->ts_ = ts;
    row->vallen_ = vallen;
    memcpy(row->s_, s_, cut);
    for (; first != last; first += 2) {
        Str val = first[1].as_s();
        memcpy(row->s_ + index_offset(first[0].as_u()), val.data(), val.length());
    }
    return row;
}

inline value_string* value_string::create(const Json* first, const Json* last,
                                          kvtimestamp_t ts, threadinfo& ti) {
    value_string empty;
    return empty.update(first, last, ts, ti);
}

inline value_string* value_string::create1(Str value,
                                           kvtimestamp_t ts,
                                           threadinfo& ti) {
    value_string* row = (value_string*) ti.allocate(shallow_size(value.length()), memtag_value);
    row->ts_ = ts;
    row->vallen_ = value.length();
    memcpy(row->s_, value.data(), value.length());
    return row;
}

inline void value_string::deallocate_rcu_after_update(const Json*, const Json*, threadinfo& ti) {
    deallocate_rcu(ti);
}

inline void value_string::deallocate_after_failed_update(const Json*, const Json*, threadinfo& ti) {
    deallocate(ti);
}

template <typename PARSER>
inline value_string* value_string::checkpoint_read(PARSER& par,
                                                   kvtimestamp_t ts,
                                                   threadinfo& ti) {
    Str str;
    par >> str;
    return create1(str, ts, ti);
}

template <typename UNPARSER>
inline void value_string::checkpoint_write(UNPARSER& unpar) const {
    unpar << Str(s_, vallen_);
}

#endif

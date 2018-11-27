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
#ifndef VALUE_ARRAY_HH
#define VALUE_ARRAY_HH
#include "compiler.hh"
#include "json.hh"

class value_array {
  public:
    typedef short index_type;
    static const char *name() { return "Array"; }

    typedef lcdf::Json Json;

    inline value_array();

    inline kvtimestamp_t timestamp() const;
    inline int ncol() const;
    inline Str col(int i) const;

    void deallocate(threadinfo &ti);
    void deallocate_rcu(threadinfo &ti);

    value_array* update(const Json* first, const Json* last, kvtimestamp_t ts, threadinfo& ti) const;
    static value_array* create(const Json* first, const Json* last, kvtimestamp_t ts, threadinfo& ti);
    static inline value_array* create1(Str value, kvtimestamp_t ts, threadinfo& ti);
    void deallocate_rcu_after_update(const Json* first, const Json* last, threadinfo& ti);
    void deallocate_after_failed_update(const Json* first, const Json* last, threadinfo& ti);

    template <typename PARSER>
    static value_array* checkpoint_read(PARSER& par, kvtimestamp_t ts,
                                        threadinfo& ti);
    template <typename UNPARSER>
    void checkpoint_write(UNPARSER& unpar) const;

    void print(FILE* f, const char* prefix, int indent, Str key,
	       kvtimestamp_t initial_ts, const char* suffix = "") {
	kvtimestamp_t adj_ts = timestamp_sub(ts_, initial_ts);
	fprintf(f, "%s%*s%.*s = ### @" PRIKVTSPARTS "%s\n", prefix, indent, "",
		key.len, key.s, KVTS_HIGHPART(adj_ts), KVTS_LOWPART(adj_ts), suffix);
    }

    static inline lcdf::inline_string* make_column(Str str, threadinfo& ti);
    static void deallocate_column(lcdf::inline_string* col, threadinfo& ti);
    static void deallocate_column_rcu(lcdf::inline_string* col, threadinfo& ti);

  private:
    kvtimestamp_t ts_;
    short ncol_;
    lcdf::inline_string* cols_[0];

    static inline size_t shallow_size(int ncol);
    inline size_t shallow_size() const;
    static value_array* make_sized_row(int ncol, kvtimestamp_t ts, threadinfo& ti);
};

inline value_array::value_array()
    : ts_(0), ncol_(0) {
}

inline kvtimestamp_t value_array::timestamp() const {
    return ts_;
}

inline int value_array::ncol() const {
    return ncol_;
}

inline Str value_array::col(int i) const {
    if (unsigned(i) < unsigned(ncol_) && cols_[i])
        return Str(cols_[i]->s, cols_[i]->len);
    else
        return Str();
}

inline size_t value_array::shallow_size(int ncol) {
    return sizeof(value_array) + sizeof(lcdf::inline_string*) * ncol;
}

inline size_t value_array::shallow_size() const {
    return shallow_size(ncol_);
}

inline lcdf::inline_string* value_array::make_column(Str str, threadinfo& ti) {
    using lcdf::inline_string;
    if (str) {
        inline_string* col = (inline_string*) ti.allocate(inline_string::size(str.length()), memtag_value);
        col->len = str.length();
        memcpy(col->s, str.data(), str.length());
        return col;
    } else
        return 0;
}

inline void value_array::deallocate_column(lcdf::inline_string* col,
                                           threadinfo& ti) {
    if (col)
        ti.deallocate(col, col->size(), memtag_value);
}

inline void value_array::deallocate_column_rcu(lcdf::inline_string* col,
                                               threadinfo& ti) {
    if (col)
        ti.deallocate_rcu(col, col->size(), memtag_value);
}

inline value_array* value_array::create(const Json* first, const Json* last,
                                        kvtimestamp_t ts, threadinfo& ti) {
    value_array empty;
    return empty.update(first, last, ts, ti);
}

inline value_array* value_array::create1(Str value, kvtimestamp_t ts, threadinfo& ti) {
    value_array* row = (value_array*) ti.allocate(shallow_size(1), memtag_value);
    row->ts_ = ts;
    row->ncol_ = 1;
    row->cols_[0] = make_column(value, ti);
    return row;
}

template <typename PARSER>
value_array* value_array::checkpoint_read(PARSER& par, kvtimestamp_t ts,
                                          threadinfo& ti) {
    unsigned ncol;
    par.read_array_header(ncol);
    value_array* row = make_sized_row(ncol, ts, ti);
    Str col;
    for (unsigned i = 0; i != ncol; i++) {
        par >> col;
        if (col)
            row->cols_[i] = make_column(col, ti);
    }
    return row;
}

template <typename UNPARSER>
void value_array::checkpoint_write(UNPARSER& unpar) const {
    unpar.write_array_header(ncol_);
    for (short i = 0; i != ncol_; i++)
        unpar << col(i);
}

#endif

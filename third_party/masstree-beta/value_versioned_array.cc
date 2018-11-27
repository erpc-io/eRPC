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
#include "kvrow.hh"
#include "value_versioned_array.hh"
#include <string.h>

value_versioned_array* value_versioned_array::make_sized_row(int ncol, kvtimestamp_t ts, threadinfo& ti) {
    value_versioned_array* row = (value_versioned_array*) ti.allocate(shallow_size(ncol), memtag_value);
    row->ts_ = ts;
    row->ver_ = rowversion();
    row->ncol_ = row->ncol_cap_ = ncol;
    memset(row->cols_, 0, sizeof(row->cols_[0]) * ncol);
    return row;
}

void value_versioned_array::snapshot(value_versioned_array*& storage,
                                     const std::vector<index_type>& f, threadinfo& ti) const {
    if (!storage || storage->ncol_cap_ < ncol_) {
        if (storage)
            storage->deallocate(ti);
        storage = make_sized_row(ncol_, ts_, ti);
    }
    storage->ncol_ = ncol_;
    rowversion v1 = ver_.stable();
    while (1) {
        if (f.size() == 1)
            storage->cols_[f[0]] = cols_[f[0]];
        else
            memcpy(storage->cols_, cols_, sizeof(cols_[0]) * storage->ncol_);
        rowversion v2 = ver_.stable();
        if (!v1.has_changed(v2))
            break;
        v1 = v2;
    }
}

value_versioned_array*
value_versioned_array::update(const Json* first, const Json* last,
                              kvtimestamp_t ts, threadinfo& ti,
                              bool always_copy) {
    int ncol = last[-2].as_u() + 1;
    value_versioned_array* row;
    if (ncol > ncol_cap_ || always_copy) {
        row = (value_versioned_array*) ti.allocate(shallow_size(ncol), memtag_value);
        row->ts_ = ts;
        row->ver_ = rowversion();
        row->ncol_ = row->ncol_cap_ = ncol;
        memcpy(row->cols_, cols_, sizeof(cols_[0]) * ncol_);
    } else
        row = this;
    if (ncol > ncol_)
        memset(row->cols_ + ncol_, 0, sizeof(cols_[0]) * (ncol - ncol_));

    if (row == this) {
        ver_.setdirty();
        fence();
    }
    if (row->ncol_ < ncol)
        row->ncol_ = ncol;

    for (; first != last; first += 2) {
        unsigned idx = first[0].as_u();
        value_array::deallocate_column_rcu(row->cols_[idx], ti);
        row->cols_[idx] = value_array::make_column(first[1].as_s(), ti);
    }

    if (row == this) {
        fence();
        ver_.clearandbump();
    }
    return row;
}

void value_versioned_array::deallocate(threadinfo &ti) {
    for (short i = 0; i < ncol_; ++i)
        value_array::deallocate_column(cols_[i], ti);
    ti.deallocate(this, shallow_size(), memtag_value);
}

void value_versioned_array::deallocate_rcu(threadinfo &ti) {
    for (short i = 0; i < ncol_; ++i)
        value_array::deallocate_column_rcu(cols_[i], ti);
    ti.deallocate_rcu(this, shallow_size(), memtag_value);
}

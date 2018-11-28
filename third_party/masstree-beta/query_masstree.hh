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
#ifndef QUERY_MASSTREE_HH
#define QUERY_MASSTREE_HH 1
#include "masstree.hh"
#include "kvrow.hh"
class threadinfo;
namespace lcdf { class Json; }

namespace Masstree {

template <typename P>
class query_table {
  public:
    typedef P parameters_type;
    typedef node_base<P> node_type;
    typedef typename P::threadinfo_type threadinfo;
    typedef unlocked_tcursor<P> unlocked_cursor_type;
    typedef tcursor<P> cursor_type;

    query_table() {
    }

    const basic_table<P>& table() const {
        return table_;
    }
    basic_table<P>& table() {
        return table_;
    }

    void initialize(threadinfo& ti) {
        table_.initialize(ti);
    }
    void destroy(threadinfo& ti) {
        table_.destroy(ti);
    }

    void findpivots(Str* pv, int npv) const;

    void stats(FILE* f);
    void json_stats(lcdf::Json& j, threadinfo& ti);
    inline lcdf::Json json_stats(threadinfo& ti) {
        lcdf::Json j;
        json_stats(j, ti);
        return j;
    }

    void print(FILE* f, int indent) const;

    static void test(threadinfo& ti);

    static const char* name() {
        return "mb";
    }

  private:
    basic_table<P> table_;
};

struct default_query_table_params : public nodeparams<15, 15> {
    typedef row_type* value_type;
    typedef value_print<value_type> value_print_type;
    typedef ::threadinfo threadinfo_type;
};

typedef query_table<default_query_table_params> default_table;

} // namespace Masstree
#endif

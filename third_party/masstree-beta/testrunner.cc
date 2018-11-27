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
#include "testrunner.hh"
#include <algorithm>
#include <numeric>
#include <vector>

testrunner_base* testrunner_base::thehead;
testrunner_base* testrunner_base::thetail;

void testrunner_base::print_names(FILE* stream, int ncol) {
    masstree_precondition(ncol >= 1);

    std::vector<lcdf::String> names;
    for (testrunner_base* tr = thehead; tr; tr = tr->next_)
        names.push_back(tr->name());

    size_t percol;
    std::vector<int> colwidth;
    while (1) {
        percol = (names.size() + ncol - 1) / ncol;
        colwidth.assign(ncol, 0);
        for (size_t i = 0; i != names.size(); ++i)
            colwidth[i/percol] = std::max(colwidth[i/percol], names[i].length());
        if (ncol == 1
            || std::accumulate(colwidth.begin(), colwidth.end(), 0)
               + ncol * 3 <= 78)
            break;
        --ncol;
    }

    for (size_t row = 0; row != percol; ++row) {
        size_t off = row;
        for (int col = 0; col != ncol; ++col, off += percol)
            if (off < names.size())
                fprintf(stream, "%*s   %s",
                        (int) (col ? colwidth[col-1] - names[off-percol].length() : 0), "",
                        names[off].c_str());
        fprintf(stream, "\n");
    }
}

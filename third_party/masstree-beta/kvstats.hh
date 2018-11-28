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
#ifndef KVSTATS_HH
#define KVSTATS_HH 1
#include <stdlib.h>

struct kvstats {
  double min, max, sum, sumsq;
  long count;
  kvstats()
    : min(-1), max(-1), sum(0), sumsq(0), count(0) {
  }
  void add(double x) {
    if (!count || x < min)
      min = x;
    if (max < x)
      max = x;
    sum += x;
    sumsq += x * x;
    count += 1;
  }
  typedef void (kvstats::*unspecified_bool_type)(double);
  operator unspecified_bool_type() const {
    return count ? &kvstats::add : 0;
  }
  void print_report(const char *name) const {
    if (count)
      printf("%s: n %ld, total %.0f, average %.0f, min %.0f, max %.0f, stddev %.0f\n",
	     name, count, sum, sum / count, min, max,
	     sqrt((sumsq - sum * sum / count) / (count - 1)));
  }
  double avg() {
    if (count)
      return sum / count;
    else
      return 0;
  }
};

#endif

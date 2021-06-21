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
#ifndef MISC_HH
#define MISC_HH
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <math.h>
#include "str.hh"
#include "timestamp.hh"
#include "clp.h"

inline void xalarm(double d) {
    double ip, fp = modf(d, &ip);
    struct itimerval x;
    timerclear(&x.it_interval);
    x.it_value.tv_sec = (long) ip;
    x.it_value.tv_usec = (long) (fp * 1000000);
    setitimer(ITIMER_REAL, &x, 0);
}

inline void napms(int n) /* nap n milliseconds */
{
  int ret;
  struct timespec req, rem;

  req.tv_sec = n / 1000;
  req.tv_nsec = (n % 1000) * 1000000;
  ret = nanosleep(&req, &rem);
  if(ret == -1 && errno != EINTR){
    perror("nanosleep");
    exit(EXIT_FAILURE);
  }
}

struct quick_istr {
    char* bbuf_;
    char buf_[32];
    quick_istr() {
        buf_[sizeof(buf_) - 1] = 0;
        set(0);
    }
    quick_istr(unsigned long x, int minlen = 0) {
        buf_[sizeof(buf_) - 1] = 0;
        set(x, minlen);
    }
    void set(unsigned long x, int minlen = 0) {
        bbuf_ = buf_ + sizeof(buf_) - 1;
        do {
            *--bbuf_ = (x % 10) + '0';
            x /= 10;
        } while (--minlen > 0 || x != 0);
    }
    lcdf::Str string() const {
        return lcdf::Str(bbuf_, buf_ + sizeof(buf_) - 1);
    }
    const char* data() const {
        return bbuf_;
    }
    size_t length() const {
        return (buf_ + sizeof(buf_) - 1) - bbuf_;
    }
    const char* c_str() const {
        return bbuf_;
    }
    bool operator==(lcdf::Str s) const {
        return s.len == int(length()) && memcmp(s.s, data(), s.len) == 0;
    }
    bool operator!=(lcdf::Str s) const {
        return !(*this == s);
    }
    static void increment_from_end(char* ends) {
        while (true) {
            --ends;
            ++*ends;
            if (*ends <= '9') {
                return;
            }
            *ends = '0';
        }
    }
    static void binary_increment_from_end(char* ends) {
        while (true) {
            --ends;
            *ends = (char) ((unsigned char) *ends + 1);
            if (*ends != 0) {
                return;
            }
        }
    }
};

struct Clp_Parser;
int clp_parse_suffixdouble(struct Clp_Parser *clp, const char *vstr,
                           int complain, void *user_data);

#endif

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
#ifndef STR_HH
#define STR_HH
#include "string_base.hh"
#include <stdarg.h>
#include <stdio.h>
namespace lcdf {

struct Str : public String_base<Str> {
    typedef Str substring_type;
    typedef Str argument_type;

    const char *s;
    int len;

    Str()
        : s(0), len(0) {
    }
    template <typename T>
    Str(const String_base<T>& x)
        : s(x.data()), len(x.length()) {
    }
    Str(const char* s_)
        : s(s_), len(strlen(s_)) {
    }
    Str(const char* s_, int len_)
        : s(s_), len(len_) {
    }
    Str(const unsigned char* s_, int len_)
        : s(reinterpret_cast<const char*>(s_)), len(len_) {
    }
    Str(const char *first, const char *last)
        : s(first), len(last - first) {
        precondition(first <= last);
    }
    Str(const unsigned char *first, const unsigned char *last)
        : s(reinterpret_cast<const char*>(first)), len(last - first) {
        precondition(first <= last);
    }
    Str(const std::string& str)
        : s(str.data()), len(str.length()) {
    }
    Str(const uninitialized_type &unused) {
        (void) unused;
    }

    static const Str maxkey;

    void assign() {
        s = 0;
        len = 0;
    }
    template <typename T>
    void assign(const String_base<T> &x) {
        s = x.data();
        len = x.length();
    }
    void assign(const char *s_) {
        s = s_;
        len = strlen(s_);
    }
    void assign(const char *s_, int len_) {
        s = s_;
        len = len_;
    }

    const char *data() const {
        return s;
    }
    int length() const {
        return len;
    }
    char* mutable_data() {
        return const_cast<char*>(s);
    }

    Str prefix(int lenx) const {
        return Str(s, lenx < len ? lenx : len);
    }
    Str substring(const char *first, const char *last) const {
        if (first <= last && first >= s && last <= s + len)
            return Str(first, last);
        else
            return Str();
    }
    Str substring(const unsigned char *first, const unsigned char *last) const {
        const unsigned char *u = reinterpret_cast<const unsigned char*>(s);
        if (first <= last && first >= u && last <= u + len)
            return Str(first, last);
        else
            return Str();
    }
    Str fast_substring(const char *first, const char *last) const {
        assert(begin() <= first && first <= last && last <= end());
        return Str(first, last);
    }
    Str fast_substring(const unsigned char *first, const unsigned char *last) const {
        assert(ubegin() <= first && first <= last && last <= uend());
        return Str(first, last);
    }
    Str ltrim() const {
        return String_generic::ltrim(*this);
    }
    Str rtrim() const {
        return String_generic::rtrim(*this);
    }
    Str trim() const {
        return String_generic::trim(*this);
    }

    long to_i() const {         // XXX does not handle negative
        long x = 0;
        int p;
        for (p = 0; p < len && s[p] >= '0' && s[p] <= '9'; ++p)
            x = (x * 10) + s[p] - '0';
        return p == len && p != 0 ? x : -1;
    }

    static Str snprintf(char *buf, size_t size, const char *fmt, ...) {
        va_list val;
        va_start(val, fmt);
        int n = vsnprintf(buf, size, fmt, val);
        va_end(val);
        return Str(buf, n);
    }
};

struct inline_string : public String_base<inline_string> {
    int len;
    char s[0];

    const char *data() const {
        return s;
    }
    int length() const {
        return len;
    }

    size_t size() const {
        return sizeof(inline_string) + len;
    }
    static size_t size(int len) {
        return sizeof(inline_string) + len;
    }
};

} // namespace lcdf

LCDF_MAKE_STRING_HASH(lcdf::Str)
LCDF_MAKE_STRING_HASH(lcdf::inline_string)
#endif

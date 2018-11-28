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
/*
 * straccum.{cc,hh} -- build up strings with operator<<
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2001-2013 Eddie Kohler
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file
 * is legally binding.
 */

#include "straccum.hh"
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
namespace lcdf {

/** @class StringAccum
 * @brief Efficiently build up Strings from pieces.
 *
 * Like the String class, StringAccum represents a string of characters.
 * However, unlike a String, a StringAccum is inherently mutable, and
 * efficiently supports building up a large string from many smaller pieces.
 *
 * StringAccum objects support operator<<() operations for most fundamental
 * data types.  A StringAccum is generally built up by operator<<(), and then
 * turned into a String by the take_string() method.  Extracting the String
 * from a StringAccum does no memory allocation or copying; the StringAccum's
 * memory is donated to the String.
 *
 * <h3>Out-of-memory StringAccums</h3>
 *
 * When there is not enough memory to add requested characters to a
 * StringAccum object, the object becomes a special "out-of-memory"
 * StringAccum. Out-of-memory objects are contagious: the result of any
 * concatenation operation involving an out-of-memory StringAccum is another
 * out-of-memory StringAccum. Calling take_string() on an out-of-memory
 * StringAccum returns an out-of-memory String.
 *
 * Note that appending an out-of-memory String to a StringAccum <em>does
 * not</em> make the StringAccum out-of-memory.
 */

/** @brief Change this StringAccum into an out-of-memory StringAccum. */
void
StringAccum::assign_out_of_memory()
{
    if (r_.cap > 0)
        delete[] reinterpret_cast<char*>(r_.s - memo_space);
    r_.s = reinterpret_cast<unsigned char*>(const_cast<char*>(String_generic::empty_data));
    r_.cap = -1;
    r_.len = 0;
}

char* StringAccum::grow(int ncap) {
    // can't append to out-of-memory strings
    if (r_.cap < 0) {
        errno = ENOMEM;
        return 0;
    }

    if (ncap < r_.cap)
        return reinterpret_cast<char*>(r_.s + r_.len);
    else if (ncap < 128)
        ncap = 128;
    else if (r_.cap < (1 << 20) && ncap < (r_.cap + memo_space) * 2 - memo_space)
        ncap = (r_.cap + memo_space) * 2 - memo_space;
    else if (r_.cap >= (1 << 20) && ncap < r_.cap + (1 << 19))
        ncap = r_.cap + (1 << 19);

    char* n = new char[ncap + memo_space];
    if (!n) {
        assign_out_of_memory();
        errno = ENOMEM;
        return 0;
    }

    n += memo_space;
    if (r_.cap > 0) {
        memcpy(n, r_.s, r_.len);
        delete[] reinterpret_cast<char*>(r_.s - memo_space);
    }
    r_.s = reinterpret_cast<unsigned char*>(n);
    r_.cap = ncap;
    return reinterpret_cast<char*>(r_.s + r_.len);
}

/** @brief Set the StringAccum's length to @a len.
    @pre @a len >= 0
    @return 0 on success, -ENOMEM on failure */
int
StringAccum::resize(int len)
{
    assert(len >= 0);
    if (len > r_.cap && !grow(len))
        return -ENOMEM;
    else {
        r_.len = len;
        return 0;
    }
}

char *
StringAccum::hard_extend(int nadjust, int nreserve)
{
    char *x;
    if (r_.len + nadjust + nreserve <= r_.cap)
        x = reinterpret_cast<char*>(r_.s + r_.len);
    else
        x = grow(r_.len + nadjust + nreserve);
    if (x)
        r_.len += nadjust;
    return x;
}

void StringAccum::transfer_from(String& x) {
    if (x.is_shared() || x._r.memo_offset != -memo_space) {
        append(x.begin(), x.end());
        x._r.deref();
    } else {
        r_.s = const_cast<unsigned char*>(x.udata());
        r_.len = x.length();
        r_.cap = x._r.memo()->capacity;
    }
}

/** @brief Null-terminate this StringAccum and return its data.

    Note that the null character does not contribute to the StringAccum's
    length(), and later append() and similar operations can overwrite it. If
    appending the null character fails, the StringAccum becomes
    out-of-memory and the returned value is a null string. */
const char *
StringAccum::c_str()
{
    if (r_.len < r_.cap || grow(r_.len))
        r_.s[r_.len] = '\0';
    return reinterpret_cast<char *>(r_.s);
}

/** @brief Append @a len copies of character @a c to the StringAccum. */
void
StringAccum::append_fill(int c, int len)
{
    if (char *s = extend(len))
        memset(s, c, len);
}

void
StringAccum::hard_append(const char *s, int len)
{
    // We must be careful about calls like "sa.append(sa.begin(), sa.end())";
    // a naive implementation might use sa's data after freeing it.
    const char *my_s = reinterpret_cast<char *>(r_.s);

    if (r_.len + len <= r_.cap) {
    success:
        memcpy(r_.s + r_.len, s, len);
        r_.len += len;
    } else if (likely(s < my_s || s >= my_s + r_.cap)) {
        if (grow(r_.len + len))
            goto success;
    } else {
        rep_t old_r = r_;
        r_ = rep_t();
        if (char *new_s = extend(old_r.len + len)) {
            memcpy(new_s, old_r.s, old_r.len);
            memcpy(new_s + old_r.len, s, len);
        }
        delete[] reinterpret_cast<char*>(old_r.s - memo_space);
    }
}

void
StringAccum::hard_append_cstr(const char *cstr)
{
    append(cstr, strlen(cstr));
}

bool
StringAccum::append_utf8_hard(int ch)
{
    if (ch < 0x8000) {
        append(static_cast<char>(0xC0 | (ch >> 6)));
        goto char1;
    } else if (ch < 0x10000) {
        if (unlikely((ch >= 0xD800 && ch < 0xE000) || ch > 0xFFFD))
            return false;
        append(static_cast<char>(0xE0 | (ch >> 12)));
        goto char2;
    } else if (ch < 0x110000) {
        append(static_cast<char>(0xF0 | (ch >> 18)));
        append(static_cast<char>(0x80 | ((ch >> 12) & 0x3F)));
    char2:
        append(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
    char1:
        append(static_cast<char>(0x80 | (ch & 0x3F)));
    } else
        return false;
    return true;
}

/** @brief Return a String object with this StringAccum's contents.

    This operation donates the StringAccum's memory to the returned String.
    After a call to take_string(), the StringAccum object becomes empty, and
    any future append() operations may cause memory allocations. If the
    StringAccum is out-of-memory, the returned String is also out-of-memory,
    but the StringAccum's out-of-memory state is reset. */
String
StringAccum::take_string()
{
    int len = length();
    int cap = r_.cap;
    char* str = reinterpret_cast<char*>(r_.s);
    if (len > 0 && cap > 0) {
        String::memo_type* memo =
            reinterpret_cast<String::memo_type*>(r_.s - memo_space);
        memo->initialize(cap, len);
        r_ = rep_t();
        return String(str, len, memo);
    } else if (!out_of_memory())
        return String();
    else {
        clear();
        return String::make_out_of_memory();
    }
}

/** @brief Swap this StringAccum's contents with @a x. */
void
StringAccum::swap(StringAccum &x)
{
    rep_t xr = x.r_;
    x.r_ = r_;
    r_ = xr;
}

/** @relates StringAccum
    @brief Append decimal representation of @a i to @a sa.
    @return @a sa */
StringAccum &
operator<<(StringAccum &sa, long i)
{
    if (char *x = sa.reserve(24)) {
        int len = sprintf(x, "%ld", i);
        sa.adjust_length(len);
    }
    return sa;
}

/** @relates StringAccum
    @brief Append decimal representation of @a u to @a sa.
    @return @a sa */
StringAccum &
operator<<(StringAccum &sa, unsigned long u)
{
    if (char *x = sa.reserve(24)) {
        int len = sprintf(x, "%lu", u);
        sa.adjust_length(len);
    }
    return sa;
}

/** @relates StringAccum
    @brief Append decimal representation of @a i to @a sa.
    @return @a sa */
StringAccum &
operator<<(StringAccum &sa, long long i)
{
    if (char *x = sa.reserve(24)) {
        int len = sprintf(x, "%lld", i);
        sa.adjust_length(len);
    }
    return sa;
}

/** @relates StringAccum
    @brief Append decimal representation of @a u to @a sa.
    @return @a sa */
StringAccum &
operator<<(StringAccum &sa, unsigned long long u)
{
    if (char *x = sa.reserve(24)) {
        int len = sprintf(x, "%llu", u);
        sa.adjust_length(len);
    }
    return sa;
}

StringAccum &
operator<<(StringAccum &sa, double d)
{
    if (char *x = sa.reserve(256)) {
        int len = sprintf(x, "%.12g", d);
        sa.adjust_length(len);
    }
    return sa;
}

/** @brief Append result of vsnprintf() to this StringAccum.
    @param n maximum number of characters to print
    @param format format argument to snprintf()
    @param val argument set
    @return *this
    @sa snprintf */
StringAccum &
StringAccum::vsnprintf(int n, const char *format, va_list val)
{
    if (char *x = reserve(n + 1)) {
#if HAVE_VSNPRINTF
        int len = ::vsnprintf(x, n + 1, format, val);
#else
        int len = vsprintf(x, format, val);
        assert(len <= n);
#endif
        adjust_length(len);
    }
    return *this;
}

/** @brief Append result of snprintf() to this StringAccum.
    @param n maximum number of characters to print
    @param format format argument to snprintf()
    @return *this

    The terminating null character is not appended to the string.

    @note The safe vsnprintf() variant is called if it exists. It does in
    the Linux kernel, and on modern Unix variants. However, if it does not
    exist on your machine, then this function is actually unsafe, and you
    should make sure that the printf() invocation represented by your
    arguments will never write more than @a n characters, not including the
    terminating null.
    @sa vsnprintf */
StringAccum &
StringAccum::snprintf(int n, const char *format, ...)
{
    va_list val;
    va_start(val, format);
    vsnprintf(n, format, val);
    va_end(val);
    return *this;
}

void
StringAccum::append_break_lines(const String& text, int linelen, const String &leftmargin)
{
    if (text.length() == 0)
        return;
    const char* line = text.begin();
    const char* ends = text.end();
    linelen -= leftmargin.length();
    for (const char* s = line; s < ends; s++) {
        const char* start = s;
        while (s < ends && isspace((unsigned char) *s))
            s++;
        const char* word = s;
        while (s < ends && !isspace((unsigned char) *s))
            s++;
        if (s - line > linelen && start > line) {
            *this << leftmargin;
            append(line, start - line);
            *this << '\n';
            line = word;
        }
    }
    if (line < text.end()) {
        *this << leftmargin;
        append(line, text.end() - line);
        *this << '\n';
    }
}

} // namespace lcdf

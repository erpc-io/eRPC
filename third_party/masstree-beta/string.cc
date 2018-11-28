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
 * string.{cc,hh} -- a String class with shared substrings
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2001-2013 Eddie Kohler
 * Copyright (c) 2008-2009 Meraki, Inc.
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

#include "string.hh"
#include "straccum.hh"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <vector>
namespace lcdf {

/** @file string.hh
 * @brief The LCDF String class.
 */

/** @class String
 * @brief A string of characters.
 *
 * The String class represents a string of characters.  Strings may be
 * constructed from C strings, characters, numbers, and so forth.  They may
 * also be added together.  The underlying character arrays are dynamically
 * allocated; String operations allocate and free memory as needed.  A String
 * and its substrings generally share memory.  Accessing a character by index
 * takes O(1) time; so does creating a substring.
 *
 * <h3>Out-of-memory strings</h3>
 *
 * When there is not enough memory to create a particular string, a special
 * "out-of-memory" string is returned instead. Out-of-memory strings are
 * contagious: the result of any concatenation operation involving an
 * out-of-memory string is another out-of-memory string. Thus, the final
 * result of a series of String operations will be an out-of-memory string,
 * even if the out-of-memory condition occurs in the middle.
 *
 * The canonical out-of-memory string is 14 bytes long, and equals the UTF-8
 * encoding of "\U0001F4A3ENOMEM\U0001F4A3" (that is, U+1F4A3 BOMB +
 * "ENOMEM" + U+1F4A3 BOMB). This sequence is unlikely to show up in normal
 * text, compares high relative to most other textual strings, and is valid
 * UTF-8.
 *
 * All canonical out-of-memory strings are equal and share the same data(),
 * which is different from the data() of any other string. See
 * String::out_of_memory_data(). The String::make_out_of_memory() function
 * returns a canonical out-of-memory string.
 *
 * Other strings may also be out-of-memory strings. For example,
 * String::make_stable(String::out_of_memory_data()) ==
 * String::make_out_of_memory(), and some (but not all) substrings of
 * out-of-memory strings are also out-of-memory strings.
 */

const char String_generic::empty_data[] = "";
// oom_data is the UTF-8 encoding of U+1F4A3 BOMB + "ENOMEM" + U+1F4A3 BOMB
const char String_generic::out_of_memory_data[] = "\360\237\222\243ENOMEM\360\237\222\243";
const char String_generic::bool_data[] = "false\0true";
const char String_generic::base64_encoding_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const unsigned char String_generic::base64_decoding_map[] =
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\x3F\0\0\0\x40"
        "\x35\x36\x37\x38\x39\x3A\x3B\x3C\x3D\x3E\0\0\0\0\0\0"
        "\0\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
        "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\0\0\0\0\0"
        "\0\x1B\x1C\x1D\x1E\x1F\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29"
        "\x2A\x2B\x2C\x2D\x2E\x2F\x30\x31\x32\x33\x34\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
const char String::int_data[] = "0\0001\0002\0003\0004\0005\0006\0007\0008\0009";

#if HAVE_STRING_PROFILING > 1
# define MEMO_INITIALIZER_TAIL , 0, 0
#else
# define MEMO_INITIALIZER_TAIL
#endif

const String::rep_type String::null_string_rep = {
    String_generic::empty_data, 0, 0
};
const String::rep_type String::oom_string_rep = {
    String_generic::out_of_memory_data, String_generic::out_of_memory_length, 0
};
const String::rep_type String::zero_string_rep = {
    &int_data[0], 0, 0
};

#if HAVE_STRING_PROFILING
uint64_t String::live_memo_count;
uint64_t String::memo_sizes[55];
uint64_t String::live_memo_sizes[55];
uint64_t String::live_memo_bytes[55];
# if HAVE_STRING_PROFILING > 1
String::memo_type *String::live_memos[55];
# endif
#endif

int
String_generic::compare(const char* a, int a_len, const char* b, int b_len)
{
    if (a != b) {
        int len = a_len < b_len ? a_len : b_len;
        int cmp = memcmp(a, b, len);
        if (cmp != 0)
            return cmp;
    }
    return a_len - b_len;
}

int String_generic::natural_compare(const char* a, int a_len,
                                    const char* b, int b_len) {
    const char* ae = a + a_len;
    const char* be = b + b_len;
    const char* aperiod = 0;
    bool aperiod_negative = false;
    int raw_compare = 0;

    while (a < ae && b < be) {
        if (isdigit((unsigned char) *a) && isdigit((unsigned char) *b)) {
            // compare the two numbers, but treat them as strings
            // (a decimal conversion might cause overflow)
            bool potential_decimal = (a == aperiod);

            // check if both are negative (note that if we get here, entire
            // string prefixes are identical)
            bool negative = false;
            if (a > ae - a_len && a[-1] == '-'
                && (a == ae - a_len + 1
                    || isspace((unsigned char) a[-2])))
                negative = true;

            // skip initial '0's, but remember any difference in length
            const char *ia = a, *ib = b;
            while (a < ae && *a == '0')
                ++a;
            while (b < be && *b == '0')
                ++b;
            int longer_zeros = (a - ia) - (b - ib);

            // walk over digits, remembering first nonidentical digit comparison
            int digit_compare = 0;
            bool a_good, b_good;
            while (1) {
                a_good = a < ae && isdigit((unsigned char) *a);
                b_good = b < be && isdigit((unsigned char) *b);
                if (!a_good || !b_good)
                    break;
                if (digit_compare == 0)
                    digit_compare = *a - *b;
                ++a;
                ++b;
            }

            // real number comparison: leading zeros are significant,
            // digit comparisons take precedence
            if (potential_decimal) {
                const char *ax = a, *bx = b;
                while (ax < ae && isdigit((unsigned char) *ax))
                    ++ax;
                while (bx < be && isdigit((unsigned char) *bx))
                    ++bx;
                // watch for IP addresses: don't treat "0.2." like a decimal
                if (!(ax + 1 < ae && *ax == '.' && !isspace((unsigned char) ax[1]))
                    && !(bx + 1 < be && *bx == '.' && !isspace((unsigned char) bx[1]))) {
                    negative = aperiod_negative;
                    if (longer_zeros)
                        return negative ? 1 : -1;
                    if (digit_compare)
                        a_good = b_good;
                }
            }
            // if one number is longer, it must also be larger
            if (a_good != b_good)
                return negative == a_good ? -1 : 1;
            // otherwise, digit comparisons take precedence
            if (digit_compare)
                return negative == (digit_compare > 0) ? -1 : 1;
            // as a last resort, the longer string of zeros is greater
            if (longer_zeros)
                return longer_zeros;
            // prepare for potential decimal comparison later
            if (!aperiod) {
                a_good = a + 1 < ae && *a == '.'
                    && isdigit((unsigned char) a[1]);
                b_good = b + 1 < be && *b == '.'
                    && isdigit((unsigned char) b[1]);
                if (a_good != b_good)
                    return negative == b_good ? 1 : -1;
                else if (a_good) {
                    aperiod = a + 1;
                    aperiod_negative = negative;
                }
            }

            // if we get here, the numeric portions were byte-for-byte
            // identical; move on
        } else if (isdigit((unsigned char) *a))
            return isalpha((unsigned char) *b) ? -1 : 1;
        else if (isdigit((unsigned char) *b))
            return isalpha((unsigned char) *a) ? 1 : -1;
        else {
            int alower = (unsigned char) tolower((unsigned char) *a);
            int blower = (unsigned char) tolower((unsigned char) *b);
            if (alower != blower)
                return alower - blower;
            if (raw_compare == 0)
                raw_compare = (unsigned char) *a - (unsigned char) *b;
            if (*a != '.')
                aperiod = 0;
            ++a;
            ++b;
        }
    }

    if ((ae - a) != (be - b))
        return (ae - a) - (be - b);
    else
        return raw_compare;
}

hashcode_t
String_generic::hashcode(const char *s, int len)
{
    if (len <= 0)
        return 0;

    uint32_t hash = len;
    int rem = hash & 3;
    const char *end = s + len - rem;
    uint32_t last16;

#if !HAVE_INDIFFERENT_ALIGNMENT
    if (!(reinterpret_cast<uintptr_t>(s) & 1)) {
#endif
#define get16(p) (*reinterpret_cast<const uint16_t *>((p)))
        for (; s != end; s += 4) {
            hash += get16(s);
            uint32_t tmp = (get16(s + 2) << 11) ^ hash;
            hash = (hash << 16) ^ tmp;
            hash += hash >> 11;
        }
        if (rem >= 2) {
            last16 = get16(s);
            goto rem2;
        }
#undef get16
#if !HAVE_INDIFFERENT_ALIGNMENT
    } else {
# if !__i386__ && !__x86_64__ && !__arch_um__
#  define get16(p) (((unsigned char) (p)[0] << 8) + (unsigned char) (p)[1])
# else
#  define get16(p) ((unsigned char) (p)[0] + ((unsigned char) (p)[1] << 8))
# endif
        // should be exactly the same as the code above
        for (; s != end; s += 4) {
            hash += get16(s);
            uint32_t tmp = (get16(s + 2) << 11) ^ hash;
            hash = (hash << 16) ^ tmp;
            hash += hash >> 11;
        }
        if (rem >= 2) {
            last16 = get16(s);
            goto rem2;
        }
# undef get16
    }
#endif

    /* Handle end cases */
    if (0) {                    // weird organization avoids uninitialized
      rem2:                     // variable warnings
        if (rem == 3) {
            hash += last16;
            hash ^= hash << 16;
            hash ^= ((unsigned char) s[2]) << 18;
            hash += hash >> 11;
        } else {
            hash += last16;
            hash ^= hash << 11;
            hash += hash >> 17;
        }
    } else if (rem == 1) {
        hash += (unsigned char) *s;
        hash ^= hash << 10;
        hash += hash >> 1;
    }

    /* Force "avalanching" of final 127 bits */
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;

    return hash;
}

int
String_generic::find_left(const char *s, int len, int start,
                          char x)
{
    if (start < 0)
        start = 0;
    for (int i = start; i < len; ++i)
        if (s[i] == x)
            return i;
    return -1;
}

int
String_generic::find_left(const char *s, int len, int start,
                          const char *x, int x_len)
{
    if (start < 0)
        start = 0;
    if (x_len == 0)
        return start <= len ? start : -1;
    int max_pos = len - x_len;
    for (int i = start; i <= max_pos; ++i)
        if (memcmp(s + i, x, x_len) == 0)
            return i;
    return -1;
}

int
String_generic::find_right(const char *s, int len, int start,
                           char x)
{
    if (start >= len)
        start = len - 1;
    for (int i = start; i >= 0; --i)
        if (s[i] == x)
            return i;
    return -1;
}

int
String_generic::find_right(const char *s, int len, int start,
                           const char *x, int x_len)
{
    if (start >= len)
        start = len - x_len;
    if (x_len == 0)
        return start >= 0 ? start : -1;
    for (int i = start; i >= 0; --i)
        if (memcmp(s + i, x, x_len) == 0)
            return i;
    return -1;
}

long String_generic::to_i(const char* s, const char* ends) {
    bool neg;
    if (s != ends && (s[0] == '-' || s[0] == '+')) {
        neg = s[0] == '-';
        ++s;
    } else
        neg = false;
    if (s == ends || !isdigit((unsigned char) *s))
        return 0;
    unsigned long x = (unsigned char) *s - '0';
    for (++s; s != ends && isdigit((unsigned char) *s); ++s)
        x = x * 10 + *s - '0';  // XXX overflow
    return neg ? -x : x;
}

bool String_generic::glob_match(const char* sbegin, int slen,
                                const char* pbegin, int plen) {
    const char* send = sbegin + slen;
    const char* pend = pbegin + plen;

    // quick common-case check for suffix matches
    while (pbegin < pend && sbegin < send
           && pend[-1] != '*' && pend[-1] != '?' && pend[-1] != ']'
           && (pbegin + 1 == pend || pend[-2] != '\\'))
        if (pend[-1] == send[-1])
            --pend, --send;
        else
            return false;

    std::vector<const char*> state, nextstate;
    state.push_back(pbegin);

    for (const char* s = sbegin; s != send && state.size(); ++s) {
        nextstate.clear();
        for (const char** pp = state.data(); pp != state.data() + state.size(); ++pp)
            if (*pp != pend) {
              reswitch:
                switch (**pp) {
                  case '?':
                    nextstate.push_back(*pp + 1);
                    break;
                  case '*':
                    if (*pp + 1 == pend)
                        return true;
                    if (nextstate.empty() || nextstate.back() != *pp)
                        nextstate.push_back(*pp);
                    ++*pp;
                    goto reswitch;
                  case '\\':
                    if (*pp + 1 != pend)
                        ++*pp;
                    goto normal_char;
                  case '[': {
                      const char *ec = *pp + 1;
                      bool negated;
                      if (ec != pend && *ec == '^') {
                          negated = true;
                          ++ec;
                      } else
                          negated = false;
                      if (ec == pend)
                          goto normal_char;

                      bool found = false;
                      do {
                          if (*++ec == *s)
                              found = true;
                      } while (ec != pend && *ec != ']');
                      if (ec == pend)
                          goto normal_char;

                      if (found == !negated)
                          nextstate.push_back(ec + 1);
                      break;
                  }
                  normal_char:
                  default:
                    if (**pp == *s)
                        nextstate.push_back(*pp + 1);
                    break;
                }
            }
        state.swap(nextstate);
    }

    for (const char** pp = state.data(); pp != state.data() + state.size(); ++pp) {
        while (*pp != pend && **pp == '*')
            ++*pp;
        if (*pp == pend)
            return true;
    }
    return false;
}


/** @cond never */
#if HAVE_STRING_PROFILING
void String::memo_type::account_new() {
    int bucket = profile_memo_size_bucket(this->dirty, this->capacity);
    ++memo_sizes[bucket];
    ++live_memo_sizes[bucket];
    live_memo_bytes[bucket] += this->capacity;
    ++live_memo_count;
# if HAVE_STRING_PROFILING > 1
    this->pprev = &live_memos[bucket];
    if ((this->next = *this->pprev))
        this->next->pprev = &this->next;
    *this->pprev = memo;
# endif
}

void String::memo_type::account_destroy() {
    int bucket = profile_memo_size_bucket(this->dirty, this->capacity);
    --live_memo_sizes[bucket];
    live_memo_bytes[bucket] -= this->capacity;
    --live_memo_count;
# if HAVE_STRING_PROFILING > 1
    if ((*this->pprev = this->next))
        this->next->pprev = this->pprev;
# endif
}
#endif

inline String::memo_type* String::create_memo(int capacity, int dirty) {
    assert(capacity > 0 && capacity >= dirty);
    memo_type *memo =
        reinterpret_cast<memo_type *>(new char[capacity + MEMO_SPACE]);
    if (memo)
        memo->initialize(capacity, dirty);
    return memo;
}

void
String::delete_memo(memo_type *memo)
{
    assert(memo->capacity > 0);
    assert(memo->capacity >= memo->dirty);
    memo->account_destroy();
    delete[] reinterpret_cast<char*>(memo);
}


#if HAVE_STRING_PROFILING
void
String::one_profile_report(StringAccum &sa, int i, int examples)
{
    if (i <= 16)
        sa << "memo_dirty_" << i;
    else if (i < 25) {
        uint32_t s = (i - 17) * 2 + 17;
        sa << "memo_cap_" << s << '_' << (s + 1);
    } else if (i < 29) {
        uint32_t s = (i - 25) * 8 + 33;
        sa << "memo_cap_" << s << '_' << (s + 7);
    } else {
        uint32_t s1 = (1U << (i - 23)) + 1;
        uint32_t s2 = (s1 - 1) << 1;
        sa << "memo_cap_" << s1 << '_' << s2;
    }
    sa << '\t' << live_memo_sizes[i] << '\t' << memo_sizes[i] << '\t' << live_memo_bytes[i] << '\n';
    if (examples) {
# if HAVE_STRING_PROFILING > 1
        for (memo_type *m = live_memos[i]; m; m = m->next) {
            sa << "    [" << m->dirty << "] ";
            uint32_t dirty = m->dirty;
            if (dirty > 0 && m->real_data[dirty - 1] == '\0')
                --dirty;
            sa.append(m->real_data, dirty > 128 ? 128 : dirty);
            sa << '\n';
        }
# endif
    }
}

void
String::profile_report(StringAccum &sa, int examples)
{
    uint64_t all_live_sizes = 0, all_sizes = 0, all_live_bytes = 0;
    for (int i = 0; i < 55; ++i) {
        if (memo_sizes[i])
            one_profile_report(sa, i, examples);
        all_live_sizes += live_memo_sizes[i];
        all_sizes += memo_sizes[i];
        all_live_bytes += live_memo_bytes[i];
    }
    sa << "memo_total\t" << all_live_sizes << '\t' << all_sizes << '\t' << all_live_bytes << '\n';
}
#endif

/** @endcond never */


/** @brief Construct a base-10 string representation of @a x. */
String::String(int x)
{
    if (x >= 0 && x < 10)
        _r.assign(int_data + 2 * x, 1, 0);
    else {
        char buf[128];
        sprintf(buf, "%d", x);
        assign(buf, -1, false);
    }
}

/** @overload */
String::String(unsigned x)
{
    if (x < 10)
        _r.assign(int_data + 2 * x, 1, 0);
    else {
        char buf[128];
        sprintf(buf, "%u", x);
        assign(buf, -1, false);
    }
}

/** @overload */
String::String(long x)
{
    if (x >= 0 && x < 10)
        _r.assign(int_data + 2 * x, 1, 0);
    else {
        char buf[128];
        sprintf(buf, "%ld", x);
        assign(buf, -1, false);
    }
}

/** @overload */
String::String(unsigned long x)
{
    if (x < 10)
        _r.assign(int_data + 2 * x, 1, 0);
    else {
        char buf[128];
        sprintf(buf, "%lu", x);
        assign(buf, -1, false);
    }
}

/** @overload */
String::String(long long x)
{
    if (x >= 0 && x < 10)
        _r.assign(int_data + 2 * x, 1, 0);
    else {
        char buf[128];
        sprintf(buf, "%lld", x);
        assign(buf, -1, false);
    }
}

/** @overload */
String::String(unsigned long long x)
{
    if (x < 10)
        _r.assign(int_data + 2 * x, 1, 0);
    else {
        char buf[128];
        sprintf(buf, "%llu", x);
        assign(buf, -1, false);
    }
}

String::String(double x)
{
    char buf[128];
    int len = sprintf(buf, "%.12g", x);
    assign(buf, len, false);
}

String
String::hard_make_stable(const char *s, int len)
{
    if (len < 0)
        len = strlen(s);
    return String(s, len, 0);
}

String
String::make_fill(int c, int len)
{
    String s;
    s.append_fill(c, len);
    return s;
}

void
String::assign_out_of_memory()
{
    _r.deref();
    _r = oom_string_rep;
}

void
String::assign(const char *s, int len, bool need_deref)
{
    if (!s) {
        assert(len <= 0);
        len = 0;
    } else if (len < 0)
        len = strlen(s);

    // need to start with dereference
    memo_type* m;
    if (need_deref) {
        if (unlikely((m = _r.memo())
                     && s >= m->real_data
                     && s + len <= m->real_data + m->capacity)) {
            // Be careful about "String s = ...; s = s.c_str();"
            _r.assign_noref(s, len, m);
            return;
        } else
            deref();
    }

    if (len == 0) {
        m = 0;
        s = String_generic::empty_data;

    } else {
        // Make the memo a multiple of 16 characters and bigger than 'len'.
        int memo_capacity = (len + 15 + MEMO_SPACE) & ~15;
        m = create_memo(memo_capacity - MEMO_SPACE, len);
        if (!m) {
            _r.reset_ref();
            assign_out_of_memory();
            return;
        }
        memcpy(m->real_data, s, len);
        s = m->real_data;
    }

    _r.assign_noref(s, len, m);
}

/** @brief Append @a len unknown characters to this string.
 * @return Modifiable pointer to the appended characters.
 *
 * The caller may safely modify the returned memory. Null is returned if
 * the string becomes out-of-memory. */
char *
String::append_uninitialized(int len)
{
    // Appending anything to "out of memory" leaves it as "out of memory"
    if (unlikely(len <= 0) || out_of_memory())
        return 0;

    // If we can, append into unused space. First, we check that there's
    // enough unused space for 'len' characters to fit; then, we check
    // that the unused space immediately follows the data in '*this'.
    uint32_t dirty;
    memo_type* m = _r.memo();
    if (m && ((dirty = m->dirty), m->capacity > dirty + len)) {
        char *real_dirty = m->real_data + dirty;
        if (real_dirty == _r.data + _r.length) {
            m->dirty = dirty + len;
            _r.length += len;
            assert(m->dirty < m->capacity);
#if HAVE_STRING_PROFILING
            profile_update_memo_dirty(m, dirty, dirty + len, m->capacity);
#endif
            return real_dirty;
        }
    }

    // Now we have to make new space. Make sure the memo is a multiple of 16
    // bytes and that it is at least 16. But for large strings, allocate a
    // power of 2, since power-of-2 sizes minimize waste in frequently-used
    // allocators, like Linux kmalloc.
    int want_memo_len = _r.length + len + MEMO_SPACE;
    int memo_capacity;
    if (want_memo_len <= 1024)
        memo_capacity = (want_memo_len + 15) & ~15;
    else
        for (memo_capacity = 2048; memo_capacity < want_memo_len; )
            memo_capacity *= 2;

    m = create_memo(memo_capacity - MEMO_SPACE, _r.length + len);
    if (!m) {
        assign_out_of_memory();
        return 0;
    }

    char *new_data = m->real_data;
    memcpy(new_data, _r.data, _r.length);

    deref();
    _r.assign_noref(new_data, _r.length + len, m);
    return const_cast<char*>(_r.data + _r.length - len);
}

void
String::append(const char* s, int len, memo_type* memo)
{
    if (!s) {
        assert(len <= 0);
        len = 0;
    } else if (len < 0)
        len = strlen(s);

    memo_type* my_memo;
    if (unlikely(len == 0) || out_of_memory())
        /* do nothing */;
    else if (unlikely(s == String_generic::out_of_memory_data) && !memo)
        // Appending "out of memory" to a regular string makes it "out of
        // memory"
        assign_out_of_memory();
    else if (_r.length == 0 && reinterpret_cast<uintptr_t>(memo) > 1) {
        deref();
        _r.assign(s, len, memo);
    } else if (likely(!((my_memo = _r.memo())
                        && s >= my_memo->real_data
                        && s + len <= my_memo->real_data + my_memo->capacity))) {
        if (char *space = append_uninitialized(len))
            memcpy(space, s, len);
    } else {
        String preserve_s(*this);
        if (char *space = append_uninitialized(len))
            memcpy(space, s, len);
    }
}

/** @brief Append @a len copies of character @a c to this string. */
void
String::append_fill(int c, int len)
{
    assert(len >= 0);
    if (char *space = append_uninitialized(len))
        memset(space, c, len);
}

/** @brief Ensure the string's data is unshared and return a mutable
    pointer to it. */
char *
String::mutable_data()
{
    // If _memo has a capacity (it's not one of the special strings) and it's
    // uniquely referenced, return _data right away.
    if (!is_shared())
        return const_cast<char *>(_r.data);

    // Otherwise, make a copy of it. Rely on: deref() doesn't change _data or
    // _length; and if _capacity == 0, then deref() doesn't free _real_data.
    // But in multithreaded situations we must hold a local copy of memo!
    String do_not_delete_underlying_memo(*this);
    deref();
    assign(_r.data, _r.length, false);
    return const_cast<char *>(_r.data);
}

const char *
String::hard_c_str() const
{
    // See also c_str().
    // We may already have a '\0' in the right place.  If _memo has no
    // capacity, then this is one of the special strings (null or
    // stable). We are guaranteed, in these strings, that _data[_length]
    // exists. Otherwise must check that _data[_length] exists.
    const char *end_data = _r.data + _r.length;
    memo_type* m = _r.memo();
    if ((m && end_data >= m->real_data + m->dirty)
        || *end_data != '\0') {
        if (char *x = const_cast<String *>(this)->append_uninitialized(1)) {
            *x = '\0';
            --_r.length;
        }
    }
    return _r.data;
}

/** @brief Null-terminate the string and return a mutable pointer to its
    data.
    @sa String::c_str */
char *
String::mutable_c_str()
{
    (void) mutable_data();
    (void) c_str();
    return const_cast<char *>(_r.data);
}

/** @brief Return a substring of this string, consisting of the @a len
    characters starting at index @a pos.
    @param pos substring's first position relative to the string
    @param len length of substring

    If @a pos is negative, starts that far from the end of the string. If @a
    len is negative, leaves that many characters off the end of the string.
    If @a pos and @a len specify a substring that is partly outside the
    string, only the part within the string is returned. If the substring is
    beyond either end of the string, returns an empty string (but this
    should be considered a programming error; a future version may generate
    a warning for this case).

    @note String::substr() is intended to behave like Perl's substr(). */
String
String::substr(int pos, int len) const
{
    if (pos < 0)
        pos += _r.length;

    int pos2;
    if (len < 0)
        pos2 = _r.length + len;
    else if (pos >= 0 && len >= _r.length) // avoid integer overflow
        pos2 = _r.length;
    else
        pos2 = pos + len;

    if (pos < 0)
        pos = 0;
    if (pos2 > _r.length)
        pos2 = _r.length;

    if (pos >= pos2)
        return String();
    else {
        _r.ref();
        return String(_r.data + pos, pos2 - pos, _r.memo());
    }
}

static String
hard_lower(const String &s, int pos)
{
    String new_s(s.data(), s.length());
    char *x = const_cast<char *>(new_s.data()); // know it's mutable
    int len = s.length();
    for (; pos < len; pos++)
        x[pos] = tolower((unsigned char) x[pos]);
    return new_s;
}

/** @brief Return a lowercased version of this string.

    Translates the ASCII characters 'A' through 'Z' into their lowercase
    equivalents. */
String
String::lower() const
{
    // avoid copies
    if (!out_of_memory())
        for (int i = 0; i < _r.length; i++)
            if (_r.data[i] >= 'A' && _r.data[i] <= 'Z')
                return hard_lower(*this, i);
    return *this;
}

static String
hard_upper(const String &s, int pos)
{
    String new_s(s.data(), s.length());
    char *x = const_cast<char *>(new_s.data()); // know it's mutable
    int len = s.length();
    for (; pos < len; pos++)
        x[pos] = toupper((unsigned char) x[pos]);
    return new_s;
}

/** @brief Return an uppercased version of this string.

    Translates the ASCII characters 'a' through 'z' into their uppercase
    equivalents. */
String
String::upper() const
{
    // avoid copies
    for (int i = 0; i < _r.length; i++)
        if (_r.data[i] >= 'a' && _r.data[i] <= 'z')
            return hard_upper(*this, i);
    return *this;
}

static String
hard_printable(const String &s, int pos, int type)
{
    StringAccum sa(s.length() * 2);
    sa.append(s.data(), pos);
    const unsigned char *x = reinterpret_cast<const unsigned char *>(s.data());
    int len = s.length();
    for (; pos < len; pos++) {
        if (type == 2 && (x[pos] == '\\' || x[pos] == '\"'))
            sa << '\\' << x[pos];
        else if (x[pos] >= 32 && x[pos] < 127)
            sa << x[pos];
        else if (x[pos] < 32 && type == 2) {
            if (x[pos] >= 9 && x[pos] <= 13)
                sa << '\\' << ("tnvfr"[x[pos] - 9]);
            else if (char *buf = sa.extend(4, 1))
                sprintf(buf, "\\%03o", x[pos]);
        } else if (x[pos] < 32 && type != 1)
            sa << '^' << (unsigned char)(x[pos] + 64);
        else if (char *buf = sa.extend(4, 1))
            sprintf(buf, "\\%03o", x[pos]);
    }
    return sa.take_string();
}

/** @brief Return a "printable" version of this string.
    @param type quoting type

    The default quoting type (0) translates control characters 0-31 into
    "control" sequences, such as "^@" for the null character, and characters
    127-255 into octal escape sequences, such as "\377" for 255. Quoting
    type 1 translates all characters outside of 32-126 into octal escape
    sequences. Quoting type 2 uses C escapes, including "\\" and "\"". */
String
String::printable(int type) const
{
    // avoid copies
    if (!out_of_memory())
        for (int i = 0; i < _r.length; i++)
            if (_r.data[i] < 32 || _r.data[i] > 126)
                return hard_printable(*this, i, type);
    return *this;
}

String String::to_hex() const {
    StringAccum sa;
    static const char hexval[] = "0123456789ABCDEF";
    char* x = sa.extend(2 * _r.length);
    for (int i = 0; i != _r.length; ++i) {
        *x++ = hexval[(unsigned char) _r.data[i] >> 4];
        *x++ = hexval[_r.data[i] & 15];
    }
    return sa.take_string();
}

/** @brief Return the substring with left whitespace removed. */
String
String::ltrim() const
{
    return String_generic::ltrim(*this);
}

/** @brief Return the substring with right whitespace removed. */
String
String::rtrim() const
{
    return String_generic::rtrim(*this);
}

/** @brief Return the substring with left and right whitespace removed. */
String
String::trim() const
{
    return String_generic::trim(*this);
}

void
String::align(int n)
{
    int offset = reinterpret_cast<uintptr_t>(_r.data) % n;
    if (offset) {
        String s;
        s.append_uninitialized(_r.length + n + 1);
        offset = reinterpret_cast<uintptr_t>(s._r.data) % n;
        memcpy((char *)s._r.data + n - offset, _r.data, _r.length);
        s._r.data += n - offset;
        s._r.length = _r.length;
        *this = s;
    }
}

/** @brief Return the pointer to the next character in UTF-8 encoding. */
const unsigned char *
String::skip_utf8_char(const unsigned char *s, const unsigned char *end)
{
    int c = *s;
    if (c > 0 && c < 0x80)
        return s + 1;
    else if (c < 0xC2)
        /* zero, or bad/overlong encoding */;
    else if (c < 0xE0) {        // 2 bytes: U+80-U+7FF
        if (likely(s + 1 < end
                   && s[1] >= 0x80 && s[1] < 0xC0))
            return s + 2;
    } else if (c < 0xF0) {      // 3 bytes: U+800-U+FFFF
        if (likely(s + 2 < end
                   && s[1] >= 0x80 && s[1] < 0xC0
                   && s[2] >= 0x80 && s[2] < 0xC0
                   && (c != 0xE0 || s[1] >= 0xA0) /* not overlong encoding */
                   && (c != 0xED || s[1] < 0xA0) /* not surrogate */))
            return s + 3;
    } else if (c < 0xF5) {      // 4 bytes: U+10000-U+10FFFF
        if (likely(s + 3 < end
                   && s[1] >= 0x80 && s[1] < 0xC0
                   && s[2] >= 0x80 && s[2] < 0xC0
                   && s[3] >= 0x80 && s[3] < 0xC0
                   && (c != 0xF0 || s[1] >= 0x90) /* not overlong encoding */
                   && (c != 0xF4 || s[1] < 0x90) /* not >U+10FFFF */))
            return s + 4;
    }
    return s;
}

int
String::parse_cesu8_char(const unsigned char *s, const unsigned char *end)
{
    if (s + 5 < end
        && s[0] == 0xED
        && s[1] >= 0xA0 && s[1] < 0xB0
        && s[2] >= 0x80 && s[2] < 0xC0
        && s[3] == 0xED
        && s[4] >= 0xB0 && s[4] < 0xC0
        && s[5] >= 0x80 && s[5] < 0xC0)
        return 0x10000
            + ((s[1] & 0x0F) << 16) + ((s[2] & 0x3F) << 10)
            + ((s[4] & 0x0F) << 6) + (s[5] & 0x3F);
    else
        return 0;
}

static const uint16_t windows1252_c1_mapping[] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x20DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

String
String::windows1252_to_utf8() const
{
    const unsigned char *s = ubegin(), *last = s, *ends = uend();
    StringAccum sa;
    for (; s != ends; ++s) {
        int c = *s;
        if (unlikely(c == 0)) {
            sa.append(last, s);
            last = s + 1;
        } else if (likely(c < 128))
            /* do nothing */;
        else {
            sa.append(last, s);
            if (unlikely(c < 160))
                c = windows1252_c1_mapping[c - 128];
            sa.append_utf8(c);
            last = s + 1;
        }
    }
    if (last == ubegin())
        return *this;
    else {
        sa.append(last, s);
        return sa.take_string();
    }
}

String
String::utf16be_to_utf8(int flags) const
{
    const unsigned char *s = ubegin(), *ends = uend();
    if ((ends - s) & 1)
        --ends;
    if ((flags & utf_strip_bom) && s != ends
        && s[0] == 0xFE && s[1] == 0xFF)
        s += 2;
    StringAccum sa;
    sa.reserve((ends - s) / 2);
    for (; s != ends; s += 2) {
        int c = s[0] * 256 + s[1];
        if (likely(c < 0xD800) || c >= 0xE000)
            sa.append_utf8(c);
        else if (c < 0xDC00 && s + 2 != ends
                 && s[2] >= 0xDC && s[2] < 0xE0) {
            c = 0x10000 + ((c - 0xD800) << 10)
                + ((s[2] & 0x03) << 8) + s[3];
            sa.append_utf8(c);
            s += 2;
        } else if (c && (flags & utf_replacement))
            sa.append_utf8(u_replacement);
    }
    return sa.take_string();
}

String
String::utf16le_to_utf8(int flags) const
{
    const unsigned char *s = ubegin(), *ends = uend();
    if ((ends - s) & 1)
        --ends;
    if ((flags & utf_strip_bom) && s != ends
        && s[1] == 0xFE && s[0] == 0xFF)
        s += 2;
    StringAccum sa;
    sa.reserve((ends - s) / 2);
    for (; s != ends; s += 2) {
        int c = s[1] * 256 + s[0];
        if (likely(c < 0xD800) || c >= 0xE000)
            sa.append_utf8(c);
        else if (c < 0xDC00 && s + 2 != ends
                 && s[3] >= 0xDC && s[3] < 0xE0) {
            c = 0x10000 + ((c - 0xD800) << 10)
                + ((s[3] & 0x03) << 8) + s[2];
            sa.append_utf8(c);
            s += 2;
        } else if (c && (flags & utf_replacement))
            sa.append_utf8(u_replacement);
    }
    return sa.take_string();
}

String
String::utf16_to_utf8(int flags) const
{
    if (length() < 2)
        return String();
    const unsigned char *s = ubegin(), *ends = uend();
    if (ends - s < 2)
        return String();
    int c = s[0] * 256 + s[1];
    if (c == 0xFEFF || (c != 0xFFFE && !(flags & utf_prefer_le)))
        return utf16be_to_utf8(flags);
    else
        return utf16le_to_utf8(flags);
}

String
String::cesu8_to_utf8(int flags) const
{
    const unsigned char *s = ubegin(), *last = s, *t, *ends = uend();
    StringAccum sa;
    if (flags & utf_strip_bom)
        s = last = skip_utf8_bom(s, ends);
    while (s != ends) {
        int c = s[0];
        if (likely(c > 0 && c < 0x7F))
            ++s;
        else if (likely((t = skip_utf8_char(s, ends)) != s))
            s = t;
        else if ((c = parse_cesu8_char(s, ends))) {
            sa.append(last, s);
            sa.append_utf8(c);
            s += 6;
            last = s;
        } else {
            sa.append(last, s);
            if ((flags & utf_replacement) && c != 0)
                sa.append_utf8(u_replacement);
            for (++s; s != ends && *s >= 0x80 && *s < 0xC0; ++s)
                /* do nothing */;
            last = s;
        }
    }
    if (last == ubegin())
        return *this;
    else {
        sa.append(last, s);
        return sa.take_string();
    }
}

String
String::utf8_to_utf8(int flags) const
{
    const unsigned char *s = ubegin(), *last = s, *t, *ends = uend();
    StringAccum sa;
    if (flags & utf_strip_bom)
        s = last = skip_utf8_bom(s, ends);
    while (s != ends) {
        int c = s[0];
        if (likely(c > 0 && c < 0x7F))
            ++s;
        else if (likely((t = skip_utf8_char(s, ends)) != s))
            s = t;
        else {
            sa.append(last, s);
            if ((flags & utf_replacement) && c != 0)
                sa.append_utf8(u_replacement);
            for (++s; s != ends && *s >= 0x80 && *s < 0xC0; ++s)
                /* do nothing */;
            last = s;
        }
    }
    if (last == ubegin())
        return *this;
    else {
        sa.append(last, s);
        return sa.take_string();
    }
}

/** @brief Return a version of the string converted to UTF-8.

    Null characters are dropped.  Then, if the result contains invalid
    UTF-8, the string is assumed to be Windows-1252 encoded, and is
    converted to UTF-8 accordingly. */
String
String::to_utf8(int flags) const
{
    const unsigned char *s = ubegin(), *ends = uend();
    if (ends - s > 2) {
        if ((s[0] == 0xFF && s[1] == 0xFE)
            || (s[0] > 0 && s[0] < 0x80 && s[1] == 0))
            return utf16le_to_utf8(flags);
        else if ((s[0] == 0xFE && s[1] == 0xFF)
                 || (s[0] == 0 && s[1] > 0 && s[1] < 0x80))
            return utf16be_to_utf8(flags);
    }

    const unsigned char *last = s, *t;
    if (ends - s > 3 && (flags & utf_strip_bom))
        s = last = skip_utf8_bom(s, ends);
    StringAccum sa;
    bool any_long_utf8 = false;
    int c;
    while (s != ends) {
        if (likely(*s > 0 && *s < 128))
            ++s;
        else if (likely((t = skip_utf8_char(s, ends)) != s)) {
            any_long_utf8 = true;
            s = t;
        } else if ((c = parse_cesu8_char(s, ends)) != 0) {
            any_long_utf8 = true;
            sa.append(last, s);
            sa.append_utf8(c);
            s += 6;
            last = s;
        } else if (*s == 0) {
            sa.append(last, s);
            ++s;
            last = s;
        } else if (!any_long_utf8)
            goto windows1252;
        else {
            sa.append(last, s);
            int c = *s;
            if (c < 160)
                c = windows1252_c1_mapping[c - 128];
            sa.append_utf8(c);
            ++s;
            last = s;
        }
    }

 exit:
    if (last == ubegin())
        return *this;
    else {
        sa.append(last, ends);
        return sa.take_string();
    }

 windows1252:
    while (s != ends) {
        if (likely(*s > 0 && *s < 128))
            ++s;
        else {
            sa.append(last, s);
            if (*s != 0) {
                int c = *s;
                if (c < 160)
                    c = windows1252_c1_mapping[c - 128];
                sa.append_utf8(c);
            }
            ++s;
            last = s;
        }
    }
    goto exit;
}

/** @brief Return this string's contents encoded for JSON.
    @pre *this is encoded in UTF-8.

    For instance, String("a\"").encode_json() == "a\\\"". Note that the
    double-quote characters that usually surround a JSON string are not
    included. */
String String::encode_json() const {
    StringAccum sa;
    const char* last = encode_json_partial(sa);
    if (last == begin())
        return *this;
    else {
        sa.append(last, end());
        return sa.take_string();
    }
}

String String::encode_base64(bool pad) const {
    StringAccum sa;
    encode_base64(sa, pad);
    return sa.take_string();
}

String String::decode_base64() const {
    StringAccum sa;
    if (!decode_base64(sa))
        return String();
    return sa.take_string();
}

String String::encode_uri_component() const {
    StringAccum sa;
    encode_uri_component(sa);
    return sa.take_string();
}

} // namespace lcdf

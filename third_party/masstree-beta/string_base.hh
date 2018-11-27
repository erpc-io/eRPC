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
#ifndef STRING_BASE_HH
#define STRING_BASE_HH
#include "compiler.hh"
#include "hashcode.hh"
#include <assert.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <iostream>
namespace lcdf {
class StringAccum;
#define LCDF_CONSTANT_CSTR(cstr) ((cstr) && __builtin_constant_p(strlen((cstr))))

class String_generic {
  public:
    static const char empty_data[1];
    static const char bool_data[11]; // "false\0true\0"
    static const char out_of_memory_data[15];
    static const char base64_encoding_table[65];
    static const unsigned char base64_decoding_map[256];
    enum { out_of_memory_length = 14 };
    static bool out_of_memory(const char* s) {
        return unlikely(s >= out_of_memory_data
                        && s <= out_of_memory_data + out_of_memory_length);
    }
    static bool equals(const char* a, int a_len, const char* b, int b_len) {
        return a_len == b_len && memcmp(a, b, a_len) == 0;
    }
    static int compare(const char* a, int a_len, const char* b, int b_len);
    static inline int compare(const unsigned char* a, int a_len,
                              const unsigned char* b, int b_len) {
        return compare(reinterpret_cast<const char*>(a), a_len,
                       reinterpret_cast<const char*>(b), b_len);
    }
    static int natural_compare(const char* a, int a_len, const char* b, int b_len);
    static int natural_compare(const unsigned char* a, int a_len,
                               const unsigned char* b, int b_len) {
        return natural_compare(reinterpret_cast<const char*>(a), a_len,
                               reinterpret_cast<const char*>(b), b_len);
    }
    static bool starts_with(const char *a, int a_len, const char *b, int b_len) {
        return a_len >= b_len && memcmp(a, b, b_len) == 0;
    }
    static int find_left(const char *s, int len, int start, char x);
    static int find_left(const char *s, int len, int start, const char *x, int x_len);
    static int find_right(const char *s, int len, int start, char x);
    static int find_right(const char *s, int len, int start, const char *x, int x_len);
    static bool glob_match(const char* s, int slen, const char* pattern, int plen);
    template <typename T> static inline typename T::substring_type ltrim(const T &str);
    template <typename T> static inline typename T::substring_type rtrim(const T &str);
    template <typename T> static inline typename T::substring_type trim(const T &str);
    static hashcode_t hashcode(const char *s, int len);
    static hashcode_t hashcode(const char *first, const char *last) {
        return hashcode(first, last - first);
    }
    static long to_i(const char* first, const char* last);
    static char upper_hex_nibble(int n) {
        return n + (n > 9 ? 'A' - 10 : '0');
    }
};

template <typename T>
class String_base {
  public:
    typedef T type;
    typedef const char* const_iterator;
    typedef const_iterator iterator;
    typedef const unsigned char* const_unsigned_iterator;
    typedef const_unsigned_iterator unsigned_iterator;
    typedef int (String_base<T>::*unspecified_bool_type)() const;

    const char* data() const {
        return static_cast<const T*>(this)->data();
    }
    int length() const {
        return static_cast<const T*>(this)->length();
    }
    int size() const {
        return static_cast<const T*>(this)->length();
    }

    /** @brief Return a pointer to the string's data as unsigned chars.

        Only the first length() characters are valid, and the string data
        might not be null-terminated. @sa data() */
    const unsigned char* udata() const {
        return reinterpret_cast<const unsigned char*>(data());
    }
    /** @brief Return an iterator for the beginning of the string.

        String iterators are simply pointers into string data, so they are
        quite efficient. @sa String::data */
    const_iterator begin() const {
        return data();
    }
    /** @brief Return an iterator for the end of the string.

        The result points one character beyond the last character in the
        string. */
    const_iterator end() const {
        return data() + length();
    }
    /** @brief Return an unsigned iterator for the beginning of the string.

        This is equivalent to reinterpret_cast<const unsigned char
        *>(begin()). */
    const_unsigned_iterator ubegin() const {
        return reinterpret_cast<const_unsigned_iterator>(data());
    }
    /** @brief Return an unsigned iterator for the end of the string.

        This is equivalent to reinterpret_cast<const unsigned char
        *>(end()). */
    const_unsigned_iterator uend() const {
        return reinterpret_cast<const_unsigned_iterator>(data() + length());
    }
    /** @brief Test if the string is nonempty. */
    operator unspecified_bool_type() const {
        return length() ? &String_base<T>::length : 0;
    }
    /** @brief Test if the string is empty. */
    bool operator!() const {
        return length() == 0;
    }
    /** @brief Test if the string is empty. */
    bool empty() const {
        return length() == 0;
    }
    /** @brief Test if the string is an out-of-memory string. */
    bool out_of_memory() const {
        return String_generic::out_of_memory(data());
    }
    /** @brief Return the @a i th character in the string.

        Does not check bounds. @sa at() */
    const char& operator[](int i) const {
        return data()[i];
    }
    /** @brief Return the @a i th character in the string.

        Checks bounds: an assertion will fail if @a i is less than 0 or not
        less than length(). @sa operator[] */
    const char& at(int i) const {
        assert(unsigned(i) < unsigned(length()));
        return data()[i];
    }
    /** @brief Return the first character in the string.

        Does not check bounds. Same as (*this)[0]. */
    const char& front() const {
        return data()[0];
    }
    /** @brief Return the last character in the string.

        Does not check bounds. Same as (*this)[length() - 1]. */
    const char& back() const {
        return data()[length() - 1];
    }
    /** @brief Test if this string is equal to the C string @a cstr. */
    bool equals(const char *cstr) const {
        return String_generic::equals(data(), length(), cstr, strlen(cstr));
    }
    /** @brief Test if this string is equal to the first @a len characters
        of @a s. */
    bool equals(const char *s, int len) const {
        return String_generic::equals(data(), length(), s, len);
    }
    /** @brief Test if this string is equal to @a x. */
    template <typename TT>
    bool equals(const String_base<TT>& x) const {
        return String_generic::equals(data(), length(), x.data(), x.length());
    }
    /** @brief Compare this string with the C string @a cstr.

        Returns 0 if this string equals @a cstr, negative if this string is
        less than @a cstr in lexicographic order, and positive if this
        string is greater than @a cstr. Lexicographic order treats
        characters as unsigned. */
    int compare(const char* cstr) const {
        return String_generic::compare(data(), length(), cstr, strlen(cstr));
    }
    /** @brief Compare this string with the first @a len characters of @a
        s. */
    int compare(const char* s, int len) const {
        return String_generic::compare(data(), length(), s, len);
    }
    /** @brief Compare this string with @a x. */
    template <typename TT>
    int compare(const String_base<TT>& x) const {
        return String_generic::compare(data(), length(), x.data(), x.length());
    }
    /** @brief Compare strings @a a and @a b. */
    template <typename TT, typename UU>
    static int compare(const String_base<TT>& a, const String_base<UU>& b) {
        return String_generic::compare(a.data(), a.length(), b.data(), b.length());
    }
    /** @brief Compare strings @a a and @a b. */
    template <typename UU>
    static int compare(const char* a, const String_base<UU> &b) {
        return String_generic::compare(a, strlen(a), b.data(), b.length());
    }
    /** @brief Compare strings @a a and @a b. */
    template <typename TT>
    static int compare(const String_base<TT>& a, const char* b) {
        return String_generic::compare(a.data(), a.length(), b, strlen(b));
    }
    /** @brief Compare strings @a a and @a b. */
    static int compare(const char* a, const char* b) {
        return String_generic::compare(a, strlen(a), b, strlen(b));
    }
    /** @brief Compare this string with the C string @a cstr using natural order.

        Natural string comparison attempts to order embedded decimal number
        reprepresentations according to their numeric order; thus, the
        string "100" compares greater than the string "2". Returns 0 if this
        string equals @a cstr (only possible if the strings are
        byte-for-byte identical), negative if this string is less than @a
        cstr in natural order, and positive if this string is greater
        than @a cstr in natural order. */
    int natural_compare(const char *cstr) const {
        return String_generic::natural_compare(data(), length(), cstr, strlen(cstr));
    }
    /** @brief Compare this string with the first @a len characters of @a
        s using natural order. */
    int natural_compare(const char *s, int len) const {
        return String_generic::natural_compare(data(), length(), s, len);
    }
    /** @brief Compare this string with @a x using natural order. */
    template <typename TT>
    int natural_compare(const String_base<TT> &x) const {
        return String_generic::natural_compare(data(), length(), x.data(), x.length());
    }
    /** @brief Compare strings @a a and @a b using natural order. */
    template <typename TT, typename UU>
    static int natural_compare(const String_base<TT> &a, const String_base<UU> &b) {
        return String_generic::natural_compare(a.data(), a.length(), b.data(), b.length());
    }
    /** @brief Compare strings @a a and @a b using natural order. */
    template <typename UU>
    static int natural_compare(const char* a, const String_base<UU> &b) {
        return String_generic::natural_compare(a, strlen(a), b.data(), b.length());
    }
    /** @brief Compare strings @a a and @a b using natural order. */
    template <typename TT>
    static int natural_compare(const String_base<TT>& a, const char* b) {
        return String_generic::natural_compare(a.data(), a.length(), b, strlen(b));
    }
    /** @brief Compare strings @a a and @a b using natural order. */
    static int natural_compare(const char* a, const char* b) {
        return String_generic::natural_compare(a, strlen(a), b, strlen(b));
    }
    /** @brief Compare strings @a a and @a b using natural order. */
    static int natural_compare(const std::string& a, const std::string& b) {
        return String_generic::natural_compare(a.data(), a.length(), b.data(), b.length());
    }
    /** @brief Comparator function object for natural string comparison. */
    class natural_comparator {
      public:
        template <typename TT, typename UU>
        bool operator()(const TT& a, const UU& b) const {
            return String_base<T>::natural_compare(a, b) < 0;
        }
    };
    /** @brief Test if this string begins with the C string @a cstr. */
    bool starts_with(const char *cstr) const {
        return String_generic::starts_with(data(), length(), cstr, strlen(cstr));
    }
    /** @brief Test if this string begins with the first @a len characters
        of @a s. */
    bool starts_with(const char *s, int len) const {
        return String_generic::starts_with(data(), length(), s, len);
    }
    /** @brief Test if this string begins with @a x. */
    template <typename TT>
    bool starts_with(const String_base<TT> &x) const {
        return String_generic::starts_with(data(), length(), x.data(), x.length());
    }
    /** @brief Search for a character in this string.

        Return the index of the leftmost occurrence of @a c, starting at
        index @a start and working up to the end of the string. Return -1 if
        the character is not found. */
    int find_left(char x, int start = 0) const {
        return String_generic::find_left(data(), length(), start, x);
    }
    /** @brief Search for the C string @a cstr as a substring in this string.

        Return the index of the leftmost occurrence of @a cstr, starting at
        index @a start. Return -1 if the substring is not found. */
    int find_left(const char *cstr, int start = 0) const {
        return String_generic::find_left(data(), length(), start, cstr, strlen(cstr));
    }
    /** @brief Search for @a x as a substring in this string.

        Return the index of the leftmost occurrence of @a x, starting at
        index @a start. Return -1 if the substring is not found. */
    template <typename TT>
    int find_left(const String_base<TT> &x, int start = 0) const {
        return String_generic::find_left(data(), length(), start, x.data(), x.length());
    }
    /** @brief Search backwards for a character in this string.

        Return the index of the rightmost occurrence of @a c, starting at
        index @a start and working up to the end of the string. Return -1 if
        the character is not found. */
    int find_right(char c, int start = INT_MAX) const {
        return String_generic::find_right(data(), length(), start, c);
    }
    /** @brief Search backwards for the C string @a cstr as a substring in
        this string.

        Return the index of the rightmost occurrence of @a cstr, starting
        at index @a start. Return -1 if the substring is not found. */
    int find_right(const char *cstr, int start = INT_MAX) const {
        return String_generic::find_right(data(), length(), start, cstr, strlen(cstr));
    }
    /** @brief Search backwards for @a x as a substring in this string.

        Return the index of the rightmost occurrence of @a x, starting at
        index @a start. Return -1 if the substring is not found. */
    template <typename TT>
    int find_right(const String_base<TT> &x, int start = INT_MAX) const {
        return String_generic::find_right(data(), length(), start, x.data(), x.length());
    }
    /** @brief Test if this string matches the glob @a pattern.

        Glob pattern syntax allows * (any number of characters), ? (one
        arbitrary character), [] (character classes, possibly negated), and
        \\ (escaping). */
    bool glob_match(const char* pattern) const {
        return String_generic::glob_match(data(), length(), pattern, strlen(pattern));
    }
    /** @overload */
    template <typename TT>
    bool glob_match(const String_base<TT>& pattern) const {
        return String_generic::glob_match(data(), length(), pattern.data(), pattern.length());
    }
    /** @brief Return a 32-bit hash function of the characters in [@a first, @a last).

        Uses Paul Hsieh's "SuperFastHash" algorithm, described at
        http://www.azillionmonkeys.com/qed/hash.html
        This hash function uses all characters in the string.

        @invariant If last1 - first1 == last2 - first2 and memcmp(first1,
        first2, last1 - first1) == 0, then hashcode(first1, last1) ==
        hashcode(first2, last2). */
    static hashcode_t hashcode(const char *first, const char *last) {
        return String_generic::hashcode(first, last);
    }
    /** @brief Return a 32-bit hash function of the characters in this string. */
    hashcode_t hashcode() const {
        return String_generic::hashcode(data(), length());
    }

    /** @brief Return the integer value of this string. */
    long to_i() const {
        return String_generic::to_i(begin(), end());
    }

    template <typename E>
    const_iterator encode_json_partial(E& e) const;
    template <typename E>
    inline void encode_json(E& e) const;
    template <typename E>
    void encode_base64(E& e, bool pad = false) const;
    template <typename E>
    bool decode_base64(E& e) const;
    template <typename E>
    void encode_uri_component(E& e) const;

    /** @brief Return this string as a std::string. */
    inline operator std::string() const {
        return std::string(begin(), end());
    }

  protected:
    String_base() = default;
};

template <typename T, typename U>
inline bool operator==(const String_base<T> &a, const String_base<U> &b) {
    return a.equals(b);
}

template <typename T>
inline bool operator==(const String_base<T> &a, const std::string &b) {
    return a.equals(b.data(), b.length());
}

template <typename T>
inline bool operator==(const std::string &a, const String_base<T> &b) {
    return b.equals(a.data(), a.length());
}

template <typename T>
inline bool operator==(const String_base<T> &a, const char *b) {
    return a.equals(b, strlen(b));
}

template <typename T>
inline bool operator==(const char *a, const String_base<T> &b) {
    return b.equals(a, strlen(a));
}

template <typename T, typename U>
inline bool operator!=(const String_base<T> &a, const String_base<U> &b) {
    return !(a == b);
}

template <typename T>
inline bool operator!=(const String_base<T> &a, const std::string &b) {
    return !(a == b);
}

template <typename T>
inline bool operator!=(const std::string &a, const String_base<T> &b) {
    return !(a == b);
}

template <typename T>
inline bool operator!=(const String_base<T> &a, const char *b) {
    return !(a == b);
}

template <typename T>
inline bool operator!=(const char *a, const String_base<T> &b) {
    return !(a == b);
}

template <typename T, typename U>
inline bool operator<(const String_base<T> &a, const String_base<U> &b) {
    return a.compare(b) < 0;
}

template <typename T, typename U>
inline bool operator<=(const String_base<T> &a, const String_base<U> &b) {
    return a.compare(b) <= 0;
}

template <typename T, typename U>
inline bool operator>=(const String_base<T> &a, const String_base<U> &b) {
    return a.compare(b) >= 0;
}

template <typename T, typename U>
inline bool operator>(const String_base<T> &a, const String_base<U> &b) {
    return a.compare(b) > 0;
}

template <typename T>
inline std::ostream &operator<<(std::ostream &f, const String_base<T> &str) {
    return f.write(str.data(), str.length());
}

template <typename T>
inline hashcode_t hashcode(const String_base<T>& x) {
    return String_generic::hashcode(x.data(), x.length());
}

// boost's spelling
template <typename T>
inline size_t hash_value(const String_base<T>& x) {
    return String_generic::hashcode(x.data(), x.length());
}

template <typename T>
inline typename T::substring_type String_generic::ltrim(const T &str) {
    const char *b = str.begin(), *e = str.end();
    while (b != e && isspace((unsigned char) b[0]))
        ++b;
    return str.fast_substring(b, e);
}

template <typename T>
inline typename T::substring_type String_generic::rtrim(const T &str) {
    const char *b = str.begin(), *e = str.end();
    while (b != e && isspace((unsigned char) e[-1]))
        --e;
    return str.fast_substring(b, e);
}

template <typename T>
inline typename T::substring_type String_generic::trim(const T &str) {
    const char *b = str.begin(), *e = str.end();
    while (b != e && isspace((unsigned char) e[-1]))
        --e;
    while (b != e && isspace((unsigned char) b[0]))
        ++b;
    return str.fast_substring(b, e);
}

#if HAVE_STD_HASH
# define LCDF_MAKE_STRING_HASH(type) \
    namespace std { template <> struct hash<type>          \
        : public unary_function<const type&, size_t> {     \
        size_t operator()(const type& x) const noexcept {  \
            return x.hashcode();                           \
        } }; }
#else
# define LCDF_MAKE_STRING_HASH(type)
#endif

template <typename T> template <typename E>
typename String_base<T>::const_iterator String_base<T>::encode_json_partial(E& enc) const {
    const char *last = this->begin(), *end = this->end();
    for (const char *s = last; s != end; ++s) {
        int c = (unsigned char) *s;

        // U+2028 and U+2029 can't appear in Javascript strings! (Though
        // they are legal in JSON strings, according to the JSON
        // definition.)
        if (unlikely(c == 0xE2)
            && s + 2 < end && (unsigned char) s[1] == 0x80
            && (unsigned char) (s[2] | 1) == 0xA9)
            c = 0x2028 + (s[2] & 1);
        else if (likely(c >= 32 && c != '\\' && c != '\"' && c != '/'))
            continue;

        enc.append(last, s);
        enc << '\\';
        switch (c) {
        case '\b':
            enc << 'b';
            break;
        case '\f':
            enc << 'f';
            break;
        case '\n':
            enc << 'n';
            break;
        case '\r':
            enc << 'r';
            break;
        case '\t':
            enc << 't';
            break;
        case '\\':
        case '\"':
        case '/':
            enc << (char) c;
            break;
        default: { // c is a control character, 0x2028, or 0x2029
            char* x = enc.extend(5);
            *x++ = 'u';
            *x++ = String_generic::upper_hex_nibble(c >> 12);
            *x++ = String_generic::upper_hex_nibble((c >> 8) & 0xF);
            *x++ = String_generic::upper_hex_nibble((c >> 4) & 0xF);
            *x++ = String_generic::upper_hex_nibble(c & 0xF);
            if (c > 255)        // skip rest of encoding of U+202[89]
                s += 2;
            break;
        }
        }
        last = s + 1;
    }
    return last;
}

template <typename T> template <typename E>
inline void String_base<T>::encode_json(E& enc) const {
    const char* last = encode_json_partial(enc);
    enc.append(last, end());
}

template <typename T> template <typename E>
void String_base<T>::encode_base64(E& enc, bool pad) const {
    char* out = enc.reserve(((length() + 2) * 4) / 3);
    const unsigned char* s = this->ubegin(), *end = this->uend();
    for (; end - s >= 3; s += 3) {
        unsigned x = (s[0] << 16) | (s[1] << 8) | s[2];
        *out++ = String_generic::base64_encoding_table[x >> 18];
        *out++ = String_generic::base64_encoding_table[(x >> 12) & 63];
        *out++ = String_generic::base64_encoding_table[(x >> 6) & 63];
        *out++ = String_generic::base64_encoding_table[x & 63];
    }
    if (end > s) {
        unsigned x = s[0] << 16;
        if (end > s + 1)
            x |= s[1] << 8;
        *out++ = String_generic::base64_encoding_table[x >> 18];
        *out++ = String_generic::base64_encoding_table[(x >> 12) & 63];
        if (end > s + 1)
            *out++ = String_generic::base64_encoding_table[(x >> 6) & 63];
        else if (pad)
            *out++ = '=';
        if (pad)
            *out++ = '=';
    }
    enc.set_end(out);
}

template <typename T> template <typename E>
bool String_base<T>::decode_base64(E& enc) const {
    char* out = enc.reserve((length() * 3) / 4 + 1);
    const unsigned char* s = this->ubegin(), *end = this->uend();
    while (end > s && end[-1] == '=')
        --end;
    for (; end - s >= 4; s += 4) {
        unsigned x = ((((unsigned) String_generic::base64_decoding_map[s[0]]) - 1) << 18)
            | ((((unsigned) String_generic::base64_decoding_map[s[1]]) - 1) << 12)
            | ((((unsigned) String_generic::base64_decoding_map[s[2]]) - 1) << 6)
            | (((unsigned) String_generic::base64_decoding_map[s[3]]) - 1);
        if ((int) x < 0)
            return false;
        *out++ = (unsigned char) (x >> 16);
        *out++ = (unsigned char) (x >> 8);
        *out++ = (unsigned char) x;
    }
    if (end - s >= 2) {
        unsigned x = ((((unsigned) String_generic::base64_decoding_map[s[0]]) - 1) << 18)
            | ((((unsigned) String_generic::base64_decoding_map[s[1]]) - 1) << 12)
            | (end - s == 3 ? (((unsigned) String_generic::base64_decoding_map[s[2]]) - 1) << 6 : 0);
        if ((int) x < 0)
            return false;
        *out++ = (unsigned char) (x >> 16);
        if (end - s == 3)
            *out++ = (unsigned char) (x >> 8);
    } else if (end - s)
        return false;
    enc.set_end(out);
    return true;
}

template <typename T> template <typename E>
void String_base<T>::encode_uri_component(E& enc) const {
    const char *last = this->begin(), *end = this->end();
    enc.reserve(end - last);
    for (const char *s = last; s != end; ++s) {
        int c = (unsigned char) *s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            continue;
        enc.append(last, s);
        char* x = enc.extend(3);
        *x++ = '%';
        *x++ = String_generic::upper_hex_nibble(c >> 4);
        *x++ = String_generic::upper_hex_nibble(c & 0xF);
        last = s + 1;
    }
    enc.append(last, end);
}

} // namespace lcdf
#endif

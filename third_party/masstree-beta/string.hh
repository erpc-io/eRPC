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
#ifndef LCDF_STRING_HH
#define LCDF_STRING_HH
#include "string_base.hh"
#include <string>
#include <utility>
namespace lcdf {

class String : public String_base<String> {
    struct memo_type;
  public:
    struct rep_type;

    enum { max_length = 0x7FFFFFE0 };

    typedef String substring_type;
    typedef const String& argument_type;

    inline String();
    inline String(const String &x);
#if HAVE_CXX_RVALUE_REFERENCES
    inline String(String &&x);
#endif
    template <typename T>
    explicit inline String(const String_base<T>& str);
    inline String(const char* cstr);
    inline String(const std::string& str);
    inline String(const char* s, int len);
    inline String(const unsigned char* s, int len);
    inline String(const char* first, const char* last);
    inline String(const unsigned char* first, const unsigned char* last);
    explicit inline String(bool x);
    explicit inline String(char c);
    explicit inline String(unsigned char c);
    explicit String(int x);
    explicit String(unsigned x);
    explicit String(long x);
    explicit String(unsigned long x);
    explicit String(long long x);
    explicit String(unsigned long long x);
    explicit String(double x);
    /** @cond never */
    inline String(const rep_type& r);
    /** @endcond never */
    inline ~String();

    static inline const String& make_empty();
    static inline const String& make_out_of_memory();
    static String make_uninitialized(int len);
    static inline String make_stable(const char* cstr);
    static inline String make_stable(const char* s, int len);
    static inline String make_stable(const char* first, const char* last);
    template <typename T>
    static inline String make_stable(const String_base<T>& str);
    static String make_fill(int c, int n);
    static inline const String& make_zero();

    inline const char* data() const;
    inline int length() const;

    inline const char *c_str() const;

    inline String substring(const char* first, const char* last) const;
    inline String substring(const unsigned char* first,
                            const unsigned char* last) const;
    inline String fast_substring(const char* first, const char* last) const;
    inline String fast_substring(const unsigned char* first,
                                 const unsigned char* last) const;
    String substr(int pos, int len) const;
    inline String substr(int pos) const;
    String ltrim() const;
    String rtrim() const;
    String trim() const;

    String lower() const;
    String upper() const;
    String printable(int type = 0) const;
    String to_hex() const;

    enum {
        utf_strip_bom = 1,
        utf_replacement = 2,
        utf_prefer_le = 4
    };

    enum {
        u_replacement = 0xFFFD
    };

    String windows1252_to_utf8() const;
    String utf16be_to_utf8(int flags = 0) const;
    String utf16le_to_utf8(int flags = 0) const;
    String utf16_to_utf8(int flags = 0) const;
    String cesu8_to_utf8(int flags = 0) const;
    String utf8_to_utf8(int flags = 0) const;
    String to_utf8(int flags = 0) const;

    using String_base<String>::encode_json;
    String encode_json() const;

    using String_base<String>::encode_base64;
    String encode_base64(bool pad = false) const;
    using String_base<String>::decode_base64;
    String decode_base64() const;

    using String_base<String>::encode_uri_component;
    String encode_uri_component() const;

    inline String& operator=(const String& x);
#if HAVE_CXX_RVALUE_REFERENCES
    inline String& operator=(String&& x);
#endif
    template <typename T>
    inline String& operator=(const String_base<T>& str);
    inline String& operator=(const char* cstr);
    inline String& operator=(const std::string& str);

    inline void assign(const String& x);
#if HAVE_CXX_RVALUE_REFERENCES
    inline void assign(String&& x);
#endif
    template <typename T>
    inline void assign(const String_base<T>& str);
    inline void assign(const char* cstr);
    inline void assign(const std::string& str);
    inline void assign(const char* first, const char* last);

    inline void swap(String& x);

    inline void append(const String& x);
    inline void append(const char* cstr);
    inline void append(const char* s, int len);
    inline void append(const char* first, const char* last);
    inline void append(const unsigned char* first, const unsigned char* last);
    void append_fill(int c, int len);
    char *append_uninitialized(int len);

    inline String& operator+=(const String& x);
    template <typename T>
    inline String& operator+=(const String_base<T>& x);
    inline String& operator+=(const char* cstr);
    inline String& operator+=(char c);

    // String operator+(String, const String &);
    // String operator+(String, const char *);
    // String operator+(const char *, const String &);

    inline bool is_shared() const;
    inline bool is_stable() const;

    inline String unique() const;
    inline void shrink_to_fit();

    char* mutable_data();
    inline unsigned char* mutable_udata();
    char* mutable_c_str();

    void align(int);

    static const unsigned char* skip_utf8_char(const unsigned char* first,
                                               const unsigned char* last);
    static const char* skip_utf8_char(const char* first, const char* last);
    static const unsigned char* skip_utf8_bom(const unsigned char* first,
                                              const unsigned char* last);
    static const char* skip_utf8_bom(const char* first, const char* last);

    /** @cond never */
    struct rep_type {
        const char* data;
        int length;
        int memo_offset;

        inline void ref() const {
            if (memo_offset)
                ++xmemo()->refcount;
        }
        inline void deref() const {
            if (memo_offset && --xmemo()->refcount == 0)
                String::delete_memo(xmemo());
        }
        inline void reset_ref() {
            memo_offset = 0;
        }

      private:
        inline memo_type* xmemo() const {
            return reinterpret_cast<memo_type*>
                (const_cast<char*>(data + memo_offset));
        }
        inline memo_type* memo() const {
            return memo_offset ? xmemo() : nullptr;
        }
        inline void assign(const char* d, int l, memo_type* m) {
            data = d;
            length = l;
            if (m) {
                ++m->refcount;
                memo_offset = static_cast<int>(reinterpret_cast<char*>(m) - d);
            } else
                memo_offset = 0;
        }
        inline void assign_noref(const char* d, int l, memo_type* m) {
            data = d;
            length = l;
            if (m)
                memo_offset = static_cast<int>(reinterpret_cast<char*>(m) - d);
            else
                memo_offset = 0;
        }
        friend class String;
        friend class StringAccum;
    };

    const rep_type& internal_rep() const {
        return _r;
    }
    void swap(rep_type& other_rep) {
        using std::swap;
        swap(_r, other_rep);
    }
    inline void assign(const rep_type& rep);
    /** @endcond never */

  private:
    /** @cond never */
    struct memo_type {
        volatile uint32_t refcount;
        uint32_t capacity;
        volatile uint32_t dirty;
#if HAVE_STRING_PROFILING > 1
        memo_type** pprev;
        memo_type* next;
#endif
        char real_data[8];      // but it might be more or less

        inline void initialize(uint32_t capacity, uint32_t dirty);
#if HAVE_STRING_PROFILING
        void account_new();
        void account_destroy();
#else
        inline void account_destroy() {}
#endif
    };

    enum {
        MEMO_SPACE = sizeof(memo_type) - 8 /* == sizeof(memo_type::real_data) */
    };

    struct null_memo {
    };
    /** @endcond never */

    mutable rep_type _r;        // mutable for c_str()

#if HAVE_STRING_PROFILING
    static uint64_t live_memo_count;
    static uint64_t memo_sizes[55];
    static uint64_t live_memo_sizes[55];
    static uint64_t live_memo_bytes[55];
# if HAVE_STRING_PROFILING > 1
    static memo_t *live_memos[55];
# endif

    static inline int profile_memo_size_bucket(uint32_t dirty, uint32_t capacity) {
        if (capacity <= 16)
            return dirty;
        else if (capacity <= 32)
            return 17 + (capacity - 17) / 2;
        else if (capacity <= 64)
            return 25 + (capacity - 33) / 8;
        else
            return 29 + 26 - ffs_msb(capacity - 1);
    }

    static void profile_update_memo_dirty(memo_type* memo, uint32_t old_dirty, uint32_t new_dirty, uint32_t capacity) {
        if (capacity <= 16 && new_dirty != old_dirty) {
            ++memo_sizes[new_dirty];
            ++live_memo_sizes[new_dirty];
            live_memo_bytes[new_dirty] += capacity;
            --live_memo_sizes[old_dirty];
            live_memo_bytes[old_dirty] -= capacity;
# if HAVE_STRING_PROFILING > 1
            if ((*memo->pprev = memo->next))
                memo->next->pprev = memo->pprev;
            memo->pprev = &live_memos[new_dirty];
            if ((memo->next = *memo->pprev))
                memo->next->pprev = &memo->next;
            *memo->pprev = memo;
# else
            (void) memo;
# endif
        }
    }

    static void one_profile_report(StringAccum &sa, int i, int examples);
#endif

    inline String(const char* data, int length, memo_type* memo) {
        _r.assign_noref(data, length, memo);
    }
    inline String(const char* data, int length, const null_memo&)
        : _r{data, length, 0} {
    }

    inline void deref() const {
        _r.deref();
    }

    void assign(const char* s, int len, bool need_deref);
    void assign_out_of_memory();
    void append(const char* s, int len, memo_type* memo);
    static String hard_make_stable(const char *s, int len);
    static inline memo_type* absent_memo() {
        return reinterpret_cast<memo_type*>(uintptr_t(1));
    }
    static inline memo_type* create_memo(int capacity, int dirty);
    static void delete_memo(memo_type* memo);
    const char* hard_c_str() const;
    bool hard_equals(const char* s, int len) const;

    static const char int_data[20];
    static const rep_type null_string_rep;
    static const rep_type oom_string_rep;
    static const rep_type zero_string_rep;

    static int parse_cesu8_char(const unsigned char* s,
                                const unsigned char* end);

    friend struct rep_type;
    friend class StringAccum;
};


/** @cond never */
inline void String::memo_type::initialize(uint32_t capacity, uint32_t dirty) {
    this->refcount = 1;
    this->capacity = capacity;
    this->dirty = dirty;
#if HAVE_STRING_PROFILING
    this->account_new();
#endif
}
/** @endcond never */

/** @brief Construct an empty String (with length 0). */
inline String::String()
    : _r{String_generic::empty_data, 0, 0} {
}

/** @brief Construct a copy of the String @a x. */
inline String::String(const String& x)
    : _r(x._r) {
    _r.ref();
}

#if HAVE_CXX_RVALUE_REFERENCES
/** @brief Move-construct a String from @a x. */
inline String::String(String &&x)
    : _r(x._r) {
    x._r.reset_ref();
}
#endif

/** @brief Construct a copy of the string @a str. */
template <typename T>
inline String::String(const String_base<T> &str) {
    assign(str.data(), str.length(), false);
}

/** @brief Construct a String containing the C string @a cstr.
    @param cstr a null-terminated C string
    @return A String containing the characters of @a cstr, up to but not
    including the terminating null character. */
inline String::String(const char* cstr) {
    if (LCDF_CONSTANT_CSTR(cstr))
        _r.assign(cstr, strlen(cstr), 0);
    else
        assign(cstr, -1, false);
}

/** @brief Construct a String containing the first @a len characters of
    string @a s.
    @param s a string
    @param len number of characters to take from @a s.  If @a len @< 0,
    then takes @c strlen(@a s) characters.
    @return A String containing @a len characters of @a s. */
inline String::String(const char* s, int len) {
    if (LCDF_CONSTANT_CSTR(s))
        _r.assign(s, len, 0);
    else
        assign(s, len, false);
}

/** @overload */
inline String::String(const unsigned char* s, int len) {
    if (LCDF_CONSTANT_CSTR(reinterpret_cast<const char*>(s)))
        _r.assign(reinterpret_cast<const char*>(s), len, 0);
    else
        assign(reinterpret_cast<const char*>(s), len, false);
}

/** @brief Construct a String containing the characters from @a first
    to @a last.
    @param first first character in string (begin iterator)
    @param last pointer one past last character in string (end iterator)
    @return A String containing the characters from @a first to @a last.

    Constructs an empty string if @a first @>= @a last. */
inline String::String(const char *first, const char *last) {
    assign(first, (first < last ? last - first : 0), false);
}

/** @overload */
inline String::String(const unsigned char* first, const unsigned char* last) {
    assign(reinterpret_cast<const char*>(first),
           (first < last ? last - first : 0), false);
}

/** @brief Construct a String from a std::string. */
inline String::String(const std::string& str) {
    assign(str.data(), str.length(), false);
}

/** @brief Construct a String equal to "true" or "false" depending on the
    value of @a x. */
inline String::String(bool x)
    : _r{String_generic::bool_data + (-x & 6), 5 - x, 0} {
    // bool_data equals "false\0true\0"
}

/** @brief Construct a String containing the single character @a c. */
inline String::String(char c) {
    assign(&c, 1, false);
}

/** @overload */
inline String::String(unsigned char c) {
    assign(reinterpret_cast<char*>(&c), 1, false);
}

inline String::String(const rep_type& r)
    : _r(r) {
    _r.ref();
}

/** @brief Destroy a String, freeing memory if necessary. */
inline String::~String() {
    deref();
}

/** @brief Return a const reference to an empty String.

    May be quicker than String::String(). */
inline const String& String::make_empty() {
    return reinterpret_cast<const String &>(null_string_rep);
}

/** @brief Return a String containing @a len unknown characters. */
inline String String::make_uninitialized(int len) {
    String s;
    s.append_uninitialized(len);
    return s;
}

/** @brief Return a const reference to the string "0". */
inline const String& String::make_zero() {
    return reinterpret_cast<const String&>(zero_string_rep);
}

/** @brief Return a String that directly references the C string @a cstr.

    The make_stable() functions are suitable for static constant strings
    whose data is known to stay around forever, such as C string constants.

    @warning The String implementation may access @a cstr's terminating null
    character. */
inline String String::make_stable(const char *cstr) {
    if (LCDF_CONSTANT_CSTR(cstr))
        return String(cstr, strlen(cstr), null_memo());
    else
        return hard_make_stable(cstr, -1);
}

/** @brief Return a String that directly references the first @a len
    characters of @a s.

    If @a len @< 0, treats @a s as a null-terminated C string.

    @warning The String implementation may access @a s[@a len], which
    should remain constant even though it's not part of the String. */
inline String String::make_stable(const char* s, int len) {
    if (__builtin_constant_p(len) && len >= 0)
        return String(s, len, null_memo());
    else
        return hard_make_stable(s, len);
}

/** @brief Return a String that directly references the character data in
    [@a first, @a last).
    @param first pointer to the first character in the character data
    @param last pointer one beyond the last character in the character data
    (but see the warning)

    This function is suitable for static constant strings whose data is
    known to stay around forever, such as C string constants.  Returns an
    empty string if @a first @>= @a last.

    @warning The String implementation may access *@a last, which should
    remain constant even though it's not part of the String. */
inline String String::make_stable(const char* first, const char* last) {
    return String(first, (first < last ? last - first : 0), null_memo());
}

/** @overload */
template <typename T>
inline String String::make_stable(const String_base<T>& str) {
    return String(str.data(), str.length(), null_memo());
}

/** @brief Return a pointer to the string's data.

    Only the first length() characters are valid, and the string
    might not be null-terminated. */
inline const char* String::data() const {
    return _r.data;
}

/** @brief Return the string's length. */
inline int String::length() const {
    return _r.length;
}

/** @brief Null-terminate the string.

    The terminating null character isn't considered part of the string, so
    this->length() doesn't change.  Returns a corresponding C string
    pointer.  The returned pointer is semi-temporary; it will persist until
    the string is destroyed or appended to. */
inline const char* String::c_str() const {
    // See also hard_c_str().
#if HAVE_OPTIMIZE_SIZE || __OPTIMIZE_SIZE__
    return hard_c_str();
#else
    // We may already have a '\0' in the right place.  If _memo has no
    // capacity, then this is one of the special strings (null or
    // stable). We are guaranteed, in these strings, that _data[_length]
    // exists. Otherwise must check that _data[_length] exists.
    const char* end_data = _r.data + _r.length;
    memo_type* m = _r.memo();
    if ((m && end_data >= m->real_data + m->dirty)
        || *end_data != '\0') {
        if (char *x = const_cast<String*>(this)->append_uninitialized(1)) {
            *x = '\0';
            --_r.length;
        }
    }
    return _r.data;
#endif
}

/** @brief Return a substring of the current string starting at @a first
    and ending before @a last.
    @param first pointer to the first substring character
    @param last pointer one beyond the last substring character

    Returns an empty string if @a first @>= @a last. Also returns an empty
    string if @a first or @a last is out of range (i.e., either less than
    this->begin() or greater than this->end()), but this should be
    considered a programming error; a future version may generate a warning
    for this case. */
inline String String::substring(const char* first, const char* last) const {
    if (first < last && first >= _r.data && last <= _r.data + _r.length) {
        _r.ref();
        return String(first, last - first, _r.memo());
    } else
        return String();
}
/** @overload */
inline String String::substring(const unsigned char* first, const unsigned char* last) const {
    return substring(reinterpret_cast<const char*>(first),
                     reinterpret_cast<const char*>(last));
}

/** @brief Return a substring of the current string starting at @a first
    and ending before @a last.
    @param first pointer to the first substring character
    @param last pointer one beyond the last substring character
    @pre begin() <= @a first <= @a last <= end() */
inline String String::fast_substring(const char* first, const char* last) const {
    assert(begin() <= first && first <= last && last <= end());
    _r.ref();
    return String(first, last - first, _r.memo());
}
/** @overload */
inline String String::fast_substring(const unsigned char* first, const unsigned char* last) const {
    return fast_substring(reinterpret_cast<const char*>(first),
                          reinterpret_cast<const char*>(last));
}

/** @brief Return the suffix of the current string starting at index @a pos.

    If @a pos is negative, starts that far from the end of the string.
    If @a pos is so negative that the suffix starts outside the string,
    then the entire string is returned. If the substring is beyond the
    end of the string (@a pos > length()), returns an empty string (but
    this should be considered a programming error; a future version may
    generate a warning for this case).

    @note String::substr() is intended to behave like Perl's
    substr(). */
inline String String::substr(int pos) const {
    return substr((pos <= -_r.length ? 0 : pos), _r.length);
}

inline void String::assign(const rep_type& rep) {
    rep.ref();
    _r.deref();
    _r = rep;
}

/** @brief Assign this string to @a x. */
inline void String::assign(const String& x) {
    assign(x._r);
}

/** @brief Assign this string to @a x. */
inline String& String::operator=(const String& x) {
    assign(x);
    return *this;
}

#if HAVE_CXX_RVALUE_REFERENCES
/** @brief Move-assign this string to @a x. */
inline void String::assign(String&& x) {
    deref();
    _r = x._r;
    x._r.reset_ref();
}

/** @brief Move-assign this string to @a x. */
inline String& String::operator=(String&& x) {
    assign(std::move(x));
    return *this;
}
#endif

/** @brief Assign this string to the C string @a cstr. */
inline void String::assign(const char* cstr) {
    if (LCDF_CONSTANT_CSTR(cstr)) {
        deref();
        _r.assign(cstr, strlen(cstr), 0);
    } else
        assign(cstr, -1, true);
}

/** @brief Assign this string to the C string @a cstr. */
inline String& String::operator=(const char* cstr) {
    assign(cstr);
    return *this;
}

/** @brief Assign this string to the string @a str. */
template <typename T>
inline void String::assign(const String_base<T>& str) {
    assign(str.data(), str.length(), true);
}

/** @brief Assign this string to the string @a str. */
template <typename T>
inline String& String::operator=(const String_base<T>& str) {
    assign(str);
    return *this;
}

/** @brief Assign this string to the std::string @a str. */
inline void String::assign(const std::string& str) {
    assign(str.data(), str.length(), true);
}

/** @brief Assign this string to the std::string @a str. */
inline String& String::operator=(const std::string& str) {
    assign(str);
    return *this;
}

/** @brief Assign this string to string [@a first, @a last). */
inline void String::assign(const char *first, const char *last) {
    assign(first, last - first, true);
}

/** @brief Swap the values of this string and @a x. */
inline void String::swap(String &x) {
    using std::swap;
    swap(_r, x._r);
}

/** @brief Append @a x to this string. */
inline void String::append(const String& x) {
    append(x.data(), x.length(), x._r.memo());
}

/** @brief Append the null-terminated C string @a cstr to this string.
    @param cstr data to append */
inline void String::append(const char* cstr) {
    if (LCDF_CONSTANT_CSTR(cstr))
        append(cstr, strlen(cstr), absent_memo());
    else
        append(cstr, -1, absent_memo());
}

/** @brief Append the first @a len characters of @a s to this string.
    @param s data to append
    @param len length of data

    If @a len @< 0, treats @a s as a null-terminated C string. */
inline void String::append(const char* s, int len) {
    append(s, len, absent_memo());
}

/** @brief Appends the data from @a first to @a last to this string.

    Does nothing if @a first @>= @a last. */
inline void String::append(const char* first, const char* last) {
    if (first < last)
        append(first, last - first);
}
/** @overload */
inline void String::append(const unsigned char* first,
                           const unsigned char* last) {
    if (first < last)
        append(reinterpret_cast<const char*>(first), last - first);
}

/** @brief Append @a x to this string.
    @return *this */
inline String& String::operator+=(const String &x) {
    append(x.data(), x.length(), x._r.memo());
    return *this;
}

/** @brief Append the null-terminated C string @a cstr to this string.
    @return *this */
inline String& String::operator+=(const char* cstr) {
    append(cstr);
    return *this;
}

/** @brief Append the character @a c to this string.
    @return *this */
inline String &String::operator+=(char c) {
    append(&c, 1);
    return *this;
}

/** @brief Append the string @a x to this string.
    @return *this */
template <typename T>
inline String &String::operator+=(const String_base<T> &x) {
    append(x.data(), x.length());
    return *this;
}

/** @brief Test if the String's data is shared or stable. */
inline bool String::is_shared() const {
    memo_type* m = _r.memo();
    return !m || m->refcount != 1;
}

/** @brief Test if the String's data is stable. */
inline bool String::is_stable() const {
    return !_r.memo();
}

/** @brief Return a unique version of this String.

    The return value shares no data with any other non-stable String. */
inline String String::unique() const {
    memo_type* m = _r.memo();
    if (!m || m->refcount == 1)
        return *this;
    else
        return String(_r.data, _r.data + _r.length);
}

/** @brief Reduce the memory allocation for this String.

    After calling this function, this String shares no more than 256 bytes
    of data with any other non-stable String. */
inline void String::shrink_to_fit() {
    memo_type* m = _r.memo();
    if (m && m->refcount > 1 && (uint32_t) _r.length + 256 < m->capacity)
        *this = String(_r.data, _r.data + _r.length);
}

/** @brief Return the unsigned char* version of mutable_data(). */
inline unsigned char* String::mutable_udata() {
    return reinterpret_cast<unsigned char*>(mutable_data());
}

/** @brief Return a const reference to a canonical out-of-memory String. */
inline const String &String::make_out_of_memory() {
    return reinterpret_cast<const String &>(oom_string_rep);
}

/** @brief Return a pointer to the next character in UTF-8 encoding.
    @pre @a first @< @a last

    If @a first doesn't point at a valid UTF-8 character, returns @a first. */
inline const char* String::skip_utf8_char(const char* first, const char* last) {
    return reinterpret_cast<const char*>(
        skip_utf8_char(reinterpret_cast<const unsigned char*>(first),
                       reinterpret_cast<const unsigned char*>(last)));
}

inline const unsigned char* String::skip_utf8_bom(const unsigned char* first,
                                                  const unsigned char* last) {
    if (last - first >= 3
        && first[0] == 0xEF && first[1] == 0xBB && first[2] == 0xBF)
        return first + 3;
    else
        return first;
}

inline const char* String::skip_utf8_bom(const char* first, const char* last) {
    return reinterpret_cast<const char*>(
        skip_utf8_bom(reinterpret_cast<const unsigned char*>(first),
                      reinterpret_cast<const unsigned char*>(last)));
}


/** @relates String
    @brief Concatenate the operands and return the result.

    At most one of the two operands can be a null-terminated C string. */
inline String operator+(String a, const String& b) {
    a += b;
    return a;
}

/** @relates String */
inline String operator+(String a, const char* b) {
    a.append(b);
    return a;
}

/** @relates String */
inline String operator+(const char* a, const String& b) {
    String s1(a);
    s1 += b;
    return s1;
}

/** @relates String
    @brief Concatenate the operands and return the result.

    The second operand is a single character. */
inline String operator+(String a, char b) {
    a.append(&b, 1);
    return a;
}

#if HAVE_CXX_USER_LITERALS
inline String operator"" _S(const char* s, size_t len) {
    return String::make_stable(s, s + len);
}
#endif

inline void swap(String& a, String& b) {
    a.swap(b);
}

} // namespace lcdf

LCDF_MAKE_STRING_HASH(lcdf::String)
#endif

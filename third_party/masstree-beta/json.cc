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
// -*- c-basic-offset: 4 -*-
#include "json.hh"
#include "compiler.hh"
#include <ctype.h>
namespace lcdf {

/** @class Json
    @brief Json data.

    The Json class represents Json data: null values, booleans, numbers,
    strings, and combinations of these primitives into arrays and objects.

    Json objects are not references, and two Json values cannot share
    subobjects. This differs from Javascript. For example:

    <code>
    Json j1 = Json::make_object(), j2 = Json::make_object();
    j1.set("a", j2); // stores a COPY of j2 in j1
    j2.set("b", 1);
    assert(j1.unparse() == "{\"a\":{}}");
    assert(j2.unparse() == "{\"b\":1}");
    </code>

    Compare this with the Javascript code:

    <code>
    var j1 = {}, j2 = {};
    j1.a = j2; // stores a REFERENCE to j2 in j1
    j2.b = 1;
    assert(JSON.stringify(j1) == "{\"a\":{\"b\":1}}");
    </code>

    Most Json functions for extracting components and typed values behave
    liberally. For example, objects silently convert to integers, and
    extracting properties from non-objects is allowed. This should make it
    easier to work with untrusted objects. (Json objects often originate
    from untrusted sources, and may not have the types you expect.) If you
    prefer an assertion to fail when a Json object has an unexpected type,
    use the checked <tt>as_</tt> and <code>at</code> functions, rather than
    the liberal <tt>to_</tt>, <tt>get</tt>, and <tt>operator[]</code>
    functions. */

const Json::null_t Json::null;
const Json Json::null_json;
static const String array_string("[Array]", 7);
static const String object_string("[Object]", 8);

// Array internals

Json::ArrayJson* Json::ArrayJson::make(int n) {
    int cap = n < 8 ? 8 : n;
    char* buf = new char[sizeof(ArrayJson) + cap * sizeof(Json)];
    return new((void*) buf) ArrayJson(cap);
}

void Json::ArrayJson::destroy(ArrayJson* aj) {
    if (aj)
        for (int i = 0; i != aj->size; ++i)
            aj->a[i].~Json();
    delete[] reinterpret_cast<char*>(aj);
}


// Object internals

Json::ObjectJson::ObjectJson(const ObjectJson &x)
    : ComplexJson(), os_(x.os_), n_(x.n_), capacity_(x.capacity_),
      hash_(x.hash_)
{
    size = x.size;
    grow(true);
}

Json::ObjectJson::~ObjectJson()
{
    ObjectItem *ob = os_, *oe = ob + n_;
    for (; ob != oe; ++ob)
        if (ob->next_ > -2)
            ob->~ObjectItem();
    delete[] reinterpret_cast<char *>(os_);
}

void Json::ObjectJson::grow(bool copy)
{
    if (copy && !capacity_)
        return;
    int new_capacity;
    if (copy)
        new_capacity = capacity_;
    else if (capacity_)
        new_capacity = capacity_ * 2;
    else
        new_capacity = 8;
    ObjectItem *new_os = reinterpret_cast<ObjectItem *>(operator new[](sizeof(ObjectItem) * new_capacity));
    ObjectItem *ob = os_, *oe = ob + n_;
    for (ObjectItem *oi = new_os; ob != oe; ++oi, ++ob) {
        if (ob->next_ == -2)
            oi->next_ = -2;
        else if (copy)
            new((void*) oi) ObjectItem(ob->v_.first, ob->v_.second, ob->next_);
        else
            memcpy(oi, ob, sizeof(ObjectItem));
    }
    if (!copy)
        operator delete[](reinterpret_cast<void *>(os_));
    os_ = new_os;
    capacity_ = new_capacity;
}

void Json::ObjectJson::rehash()
{
    hash_.assign(hash_.size() * 2, -1);
    for (int i = n_ - 1; i >= 0; --i) {
        ObjectItem &oi = item(i);
        if (oi.next_ > -2) {
            int b = bucket(oi.v_.first.data(), oi.v_.first.length());
            oi.next_ = hash_[b];
            hash_[b] = i;
        }
    }
}

int Json::ObjectJson::find_insert(const String &key, const Json &value)
{
    if (hash_.empty())
        hash_.assign(8, -1);
    int *b = &hash_[bucket(key.data(), key.length())], chain = 0;
    while (*b >= 0 && os_[*b].v_.first != key) {
        b = &os_[*b].next_;
        ++chain;
    }
    if (*b >= 0)
        return *b;
    else {
        *b = n_;
        if (n_ == capacity_)
            grow(false);
        // NB 'b' is invalid now
        new ((void *) &os_[n_]) ObjectItem(key, value, -1);
        ++n_;
        ++size;
        if (chain > 4)
            rehash();
        return n_ - 1;
    }
}

Json &Json::ObjectJson::get_insert(Str key)
{
    if (hash_.empty())
        hash_.assign(8, -1);
    int *b = &hash_[bucket(key.data(), key.length())], chain = 0;
    while (*b >= 0 && os_[*b].v_.first != key) {
        b = &os_[*b].next_;
        ++chain;
    }
    if (*b >= 0)
        return os_[*b].v_.second;
    else {
        *b = n_;
        if (n_ == capacity_)
            grow(false);
        // NB 'b' is invalid now
        new ((void *) &os_[n_]) ObjectItem(String(key.data(), key.length()), null_json, -1);
        ++n_;
        ++size;
        if (chain > 4)
            rehash();
        return os_[n_ - 1].v_.second;
    }
}

void Json::ObjectJson::erase(int p) {
    const ObjectItem& oi = item(p);
    int* b = &hash_[bucket(oi.v_.first.data(), oi.v_.first.length())];
    while (*b >= 0 && *b != p)
        b = &os_[*b].next_;
    assert(*b == p);
    *b = os_[p].next_;
    os_[p].~ObjectItem();
    os_[p].next_ = -2;
    --size;
}

Json::size_type Json::ObjectJson::erase(Str key) {
    int* b = &hash_[bucket(key.data(), key.length())];
    while (*b >= 0 && os_[*b].v_.first != key)
        b = &os_[*b].next_;
    if (*b >= 0) {
        int p = *b;
        *b = os_[p].next_;
        os_[p].~ObjectItem();
        os_[p].next_ = -2;
        --size;
        return 1;
    } else
        return 0;
}

namespace {
template <typename T> bool string_to_int_key(const char *first,
                                             const char *last, T& x)
{
    if (first == last || !isdigit((unsigned char) *first)
        || (first[0] == '0' && first + 1 != last))
        return false;
    // XXX integer overflow
    x = *first - '0';
    for (++first; first != last && isdigit((unsigned char) *first); ++first)
        x = 10 * x + *first - '0';
    return first == last;
}
}

void Json::hard_uniqueify_array(bool convert, int ncap_in) {
    if (!convert)
        precondition(is_null() || is_array());

    rep_type old_u = u_;

    unsigned ncap = std::max(ncap_in, 8);
    if (old_u.x.type == j_array && old_u.a.x)
        ncap = std::max(ncap, unsigned(old_u.a.x->size));
    // New capacity: Round up to a power of 2, up to multiples of 1<<14.
    unsigned xcap = iceil_log2(ncap);
    if (xcap <= (1U << 14))
        ncap = xcap;
    else
        ncap = ((ncap - 1) | ((1U << 14) - 1)) + 1;
    u_.a.x = ArrayJson::make(ncap);
    u_.a.type = j_array;

    if (old_u.x.type == j_array && old_u.a.x && old_u.a.x->refcount == 1) {
        u_.a.x->size = old_u.a.x->size;
        memcpy(u_.a.x->a, old_u.a.x->a, sizeof(Json) * u_.a.x->size);
        delete[] reinterpret_cast<char*>(old_u.a.x);
    } else if (old_u.x.type == j_array && old_u.a.x) {
        u_.a.x->size = old_u.a.x->size;
        Json* last = u_.a.x->a + u_.a.x->size;
        for (Json* it = u_.a.x->a, *oit = old_u.a.x->a; it != last; ++it, ++oit)
            new((void*) it) Json(*oit);
        old_u.a.x->deref(j_array);
    } else if (old_u.x.type == j_object && old_u.o.x) {
        ObjectItem *ob = old_u.o.x->os_, *oe = ob + old_u.o.x->n_;
        unsigned i;
        for (; ob != oe; ++ob)
            if (ob->next_ > -2
                && string_to_int_key(ob->v_.first.begin(),
                                     ob->v_.first.end(), i)) {
                if (i >= unsigned(u_.a.x->capacity))
                    hard_uniqueify_array(false, i + 1);
                if (i >= unsigned(u_.a.x->size)) {
                    memset(&u_.a.x->a[u_.a.x->size], 0, sizeof(Json) * (i + 1 - u_.a.x->size));
                    u_.a.x->size = i + 1;
                }
                u_.a.x->a[i] = ob->v_.second;
            }
        old_u.o.x->deref(j_object);
    } else if (old_u.x.type < 0)
        old_u.str.deref();
}

void Json::hard_uniqueify_object(bool convert) {
    if (!convert)
        precondition(is_null() || is_object());
    ObjectJson* noj;
    if (u_.x.type == j_object && u_.o.x) {
        noj = new ObjectJson(*u_.o.x);
        u_.o.x->deref(j_object);
    } else if (u_.x.type == j_array && u_.a.x) {
        noj = new ObjectJson;
        for (int i = 0; i != u_.a.x->size; ++i)
            noj->find_insert(String(i), u_.a.x->a[i]);
        u_.a.x->deref(j_array);
    } else {
        noj = new ObjectJson;
        if (u_.x.type < 0)
            u_.str.deref();
    }
    u_.o.x = noj;
    u_.o.type = j_object;
}

void Json::clear() {
    static_assert(offsetof(rep_type, i.type) == offsetof(rep_type, x.type), "odd Json::rep_type.i.type offset");
    static_assert(offsetof(rep_type, u.type) == offsetof(rep_type, x.type), "odd Json::rep_type.u.type offset");
    static_assert(offsetof(rep_type, d.type) == offsetof(rep_type, x.type), "odd Json::rep_type.d.type offset");
    static_assert(offsetof(rep_type, str.memo_offset) == offsetof(rep_type, x.type), "odd Json::rep_type.str.memo_offset offset");
    static_assert(offsetof(rep_type, a.type) == offsetof(rep_type, x.type), "odd Json::rep_type.a.type offset");
    static_assert(offsetof(rep_type, o.type) == offsetof(rep_type, x.type), "odd Json::rep_type.o.type offset");

    if (u_.x.type == j_array) {
        if (u_.a.x && u_.a.x->refcount == 1) {
            Json* last = u_.a.x->a + u_.a.x->size;
            for (Json* it = u_.a.x->a; it != last; ++it)
                it->~Json();
            u_.a.x->size = 0;
        } else if (u_.a.x) {
            u_.a.x->deref(j_array);
            u_.a.x = 0;
        }
    } else if (u_.x.type == j_object) {
        if (u_.o.x && u_.o.x->refcount == 1) {
            ObjectItem* last = u_.o.x->os_ + u_.o.x->n_;
            for (ObjectItem* it = u_.o.x->os_; it != last; ++it)
                if (it->next_ != -2)
                    it->~ObjectItem();
            u_.o.x->n_ = u_.o.x->size = 0;
            u_.o.x->hash_.assign(u_.o.x->hash_.size(), -1);
        } else if (u_.o.x) {
            u_.o.x->deref(j_object);
            u_.o.x = 0;
        }
    } else {
        if (u_.x.type < 0)
            u_.str.deref();
        memset(&u_, 0, sizeof(u_));
    }
}

void* Json::uniqueify_array_insert(bool convert, size_type pos) {
    size_type size = u_.a.x ? u_.a.x->size : 0;
    uniqueify_array(convert, size + 1);
    if (pos == (size_type) -1)
        pos = size;
    precondition(pos >= 0 && pos <= size);
    if (pos != size)
        memmove(&u_.a.x->a[pos + 1], &u_.a.x->a[pos], (size - pos) * sizeof(Json));
    ++u_.a.x->size;
    return (void*) &u_.a.x->a[pos];
}

Json::array_iterator Json::erase(array_iterator first, array_iterator last) {
    if (first < last) {
        uniqueify_array(false, 0);
        size_type fpos = first - abegin();
        size_type lpos = last - abegin();
        size_type size = u_.a.x->size;
        for (size_type pos = fpos; pos != lpos; ++pos)
            u_.a.x->a[pos].~Json();
        if (lpos != size)
            memmove(&u_.a.x->a[fpos], &u_.a.x->a[lpos],
                    (size - lpos) * sizeof(Json));
        u_.a.x->size -= lpos - fpos;
    }
    return first;
}

/** @brief Reserve the array Json to hold at least @a n items. */
void Json::reserve(size_type n) {
    uniqueify_array(false, n);
}

/** @brief Resize the array Json to size @a n. */
void Json::resize(size_type n) {
    uniqueify_array(false, n);
    while (u_.a.x->size > n && u_.a.x->size > 0) {
        --u_.a.x->size;
        u_.a.x->a[u_.a.x->size].~Json();
    }
    while (u_.a.x->size < n)
        push_back(Json());
}


// Primitives

int64_t Json::hard_to_i() const {
    switch (u_.x.type) {
    case j_array:
    case j_object:
        return size();
    case j_bool:
    case j_int:
        return u_.i.x;
    case j_unsigned:
        return u_.u.x;
    case j_double:
        return int64_t(u_.d.x);
    case j_null:
    case j_string:
    default:
        if (!u_.x.x)
            return 0;
        invariant(u_.x.type <= 0);
        const char *b = reinterpret_cast<const String&>(u_.str).c_str();
        char *s;
#if SIZEOF_LONG >= 8
        long x = strtol(b, &s, 0);
#else
        long long x = strtoll(b, &s, 0);
#endif
        if (s == b + u_.str.length)
            return x;
        else
            return (long) strtod(b, 0);
    }
}

uint64_t Json::hard_to_u() const {
    switch (u_.x.type) {
    case j_array:
    case j_object:
        return size();
    case j_bool:
    case j_int:
        return u_.i.x;
    case j_unsigned:
        return u_.u.x;
    case j_double:
        return uint64_t(u_.d.x);
    case j_null:
    case j_string:
    default:
        if (!u_.x.x)
            return 0;
        const char* b = reinterpret_cast<const String&>(u_.str).c_str();
        char *s;
#if SIZEOF_LONG >= 8
        unsigned long x = strtoul(b, &s, 0);
#else
        unsigned long long x = strtoull(b, &s, 0);
#endif
        if (s == b + u_.str.length)
            return x;
        else
            return (uint64_t) strtod(b, 0);
    }
}

double Json::hard_to_d() const {
    switch (u_.x.type) {
    case j_array:
    case j_object:
        return size();
    case j_bool:
    case j_int:
        return u_.i.x;
    case j_unsigned:
        return u_.u.x;
    case j_double:
        return u_.d.x;
    case j_null:
    case j_string:
    default:
        if (!u_.x.x)
            return 0;
        else
            return strtod(reinterpret_cast<const String&>(u_.str).c_str(), 0);
    }
}

bool Json::hard_to_b() const {
    switch (u_.x.type) {
    case j_array:
    case j_object:
        return !empty();
    case j_bool:
    case j_int:
    case j_unsigned:
        return u_.i.x != 0;
    case j_double:
        return u_.d.x;
    case j_null:
    case j_string:
    default:
        return u_.str.length != 0;
    }
}

String Json::hard_to_s() const {
    switch (u_.x.type) {
    case j_array:
        return array_string;
    case j_object:
        return object_string;
    case j_bool:
        return String(bool(u_.i.x));
    case j_int:
        return String(u_.i.x);
    case j_unsigned:
        return String(u_.u.x);
    case j_double:
        return String(u_.d.x);
    case j_null:
    case j_string:
    default:
        if (!u_.x.x)
            return String::make_empty();
        else
            return String(u_.str);
    }
}

const Json& Json::hard_get(Str key) const {
    ArrayJson *aj;
    unsigned i;
    if (is_array() && (aj = ajson())
        && string_to_int_key(key.begin(), key.end(), i)
        && i < unsigned(aj->size))
        return aj->a[i];
    else
        return make_null();
}

const Json& Json::hard_get(size_type x) const {
    if (is_object() && u_.o.x)
        return get(String(x));
    else
        return make_null();
}

Json& Json::hard_get_insert(size_type x) {
    if (is_object())
        return get_insert(String(x));
    else {
        uniqueify_array(true, x + 1);
        if (u_.a.x->size <= x) {
            memset(&u_.a.x->a[u_.a.x->size], 0, sizeof(Json) * (x + 1 - u_.a.x->size));
            u_.a.x->size = x + 1;
        }
        return u_.a.x->a[x];
    }
}

bool operator==(const Json& a, const Json& b) {
    if ((a.u_.x.type > 0 || b.u_.x.type > 0)
        && a.u_.x.type != b.u_.x.type)
        return a.u_.u.x == b.u_.u.x
            && a.u_.i.x >= 0
            && a.is_int()
            && b.is_int();
    else if (a.u_.x.type == Json::j_int
             || a.u_.x.type == Json::j_unsigned
             || a.u_.x.type == Json::j_bool)
        return a.u_.u.x == b.u_.u.x;
    else if (a.u_.x.type == Json::j_double)
        return a.u_.d.x == b.u_.d.x;
    else if (a.u_.x.type > 0 || !a.u_.x.x || !b.u_.x.x)
        return a.u_.x.x == b.u_.x.x;
    else
        return String(a.u_.str) == String(b.u_.str);
}


// Unparsing

Json::unparse_manipulator Json::default_manipulator;

bool Json::unparse_is_complex() const {
    if (is_object()) {
        if (ObjectJson *oj = ojson()) {
            if (oj->size > 5)
                return true;
            ObjectItem *ob = oj->os_, *oe = ob + oj->n_;
            for (; ob != oe; ++ob)
                if (ob->next_ > -2 && !ob->v_.second.empty() && !ob->v_.second.is_primitive())
                    return true;
        }
    } else if (is_array()) {
        if (ArrayJson *aj = ajson()) {
            if (aj->size > 8)
                return true;
            for (Json* it = aj->a; it != aj->a + aj->size; ++it)
                if (!it->empty() && !it->is_primitive())
                    return true;
        }
    }
    return false;
}

void Json::unparse_indent(StringAccum &sa, const unparse_manipulator &m, int depth)
{
    sa << '\n';
    depth *= (m.tab_width() ? m.tab_width() : 8);
    sa.append_fill('\t', depth / 8);
    sa.append_fill(' ', depth % 8);
}

namespace {
const char* const upx_normal[] = {":", ","};
const char* const upx_expanded[] = {": ", ","};
const char* const upx_separated[] = {": ", ", "};
}

void Json::hard_unparse(StringAccum &sa, const unparse_manipulator &m, int depth) const
{
    if (is_object() || is_array()) {
        bool expanded = depth < m.indent_depth() && unparse_is_complex();
        const char* const* upx;
        if (expanded)
            upx = upx_expanded;
        else if (m.space_separator())
            upx = upx_separated;
        else
            upx = upx_normal;

        if (is_object() && !u_.x.x)
            sa << "{}";
        else if (is_object()) {
            sa << '{';
            bool rest = false;
            ObjectJson *oj = ojson();
            ObjectItem *ob = oj->os_, *oe = ob + oj->n_;
            for (; ob != oe; ++ob)
                if (ob->next_ > -2) {
                    if (rest)
                        sa << upx[1];
                    if (expanded)
                        unparse_indent(sa, m, depth + 1);
                    sa << '\"';
                    ob->v_.first.encode_json(sa);
                    sa << '\"' << upx[0];
                    ob->v_.second.hard_unparse(sa, m, depth + 1);
                    rest = true;
                }
            if (expanded)
                unparse_indent(sa, m, depth);
            sa << '}';
        } else if (!u_.x.x)
            sa << "[]";
        else {
            sa << '[';
            bool rest = false;
            ArrayJson* aj = ajson();
            for (Json* it = aj->a; it != aj->a + aj->size; ++it) {
                if (rest)
                    sa << upx[1];
                if (expanded)
                    unparse_indent(sa, m, depth + 1);
                it->hard_unparse(sa, m, depth + 1);
                rest = true;
            }
            if (expanded)
                unparse_indent(sa, m, depth);
            sa << ']';
        }
    } else if (u_.x.type == j_null && !u_.x.x)
        sa.append("null", 4);
    else if (u_.x.type <= 0) {
        sa << '\"';
        reinterpret_cast<const String&>(u_.str).encode_json(sa);
        sa << '\"';
    } else if (u_.x.type == j_bool) {
        bool b = u_.i.x;
        sa.append(&"false\0true"[-b & 6], 5 - b);
    } else if (u_.x.type == j_int)
        sa << u_.i.x;
    else if (u_.x.type == j_unsigned)
        sa << u_.u.x;
    else if (u_.x.type == j_double)
        sa << u_.d.x;

    if (depth == 0 && m.newline_terminator())
        sa << '\n';
}


bool
Json::assign_parse(const char* first, const char* last, const String& str)
{
    using std::swap;
    Json::streaming_parser jsp;
    first = jsp.consume(first, last, str, true);
    if (first != last && (*first == ' ' || *first == '\n' || *first == '\r'
                          || *first == '\t'))
        ++first;
    if (first == last && jsp.success()) {
        swap(jsp.result(), *this);
        return true;
    } else
        return false;
}

static inline bool in_range(uint8_t x, unsigned low, unsigned high) {
    return (unsigned) x - low < high - low;
}

static inline bool in_range(int x, unsigned low, unsigned high) {
    return (unsigned) x - low < high - low;
}

inline const uint8_t* Json::streaming_parser::error_at(const uint8_t* here) {
    state_ = st_error;
    return here;
}

inline Json* Json::streaming_parser::current() {
    return stack_.empty() ? &json_ : stack_.back();
}

const uint8_t*
Json::streaming_parser::consume(const uint8_t* first,
                                const uint8_t* last,
                                const String& str,
                                bool complete) {
    using std::swap;
    Json j;

    if (state_ >= 0 && (state_ & st_partmask)) {
        if ((state_ & st_stringpart) && state_ >= st_object_colon)
            goto string_object_key;
        else if (state_ & st_stringpart)
            goto string_value;
        else if (state_ & st_primitivepart)
            goto primitive;
        else
            goto number;
    }

    while (state_ >= 0 && first != last)
        switch (*first) {
        case ' ':
        case '\n':
        case '\r':
        case '\t':
            while (first != last && *first <= 32
                   && (*first == ' ' || *first == '\n' || *first == '\r'
                       || *first == '\t'))
                ++first;
            break;

        case ',':
            if (state_ == st_array_delim)
                state_ = st_array_value;
            else if (state_ == st_object_delim)
                state_ = st_object_key;
            else
                goto error_here;
            ++first;
            break;

        case ':':
            if (state_ == st_object_colon) {
                state_ = st_object_value;
                ++first;
                break;
            } else
                goto error_here;

        case '{':
            if (state_ <= st_object_value) {
                if (stack_.size() == max_depth)
                    goto error_here;
                ++first;
                if (state_ == st_initial && json_.is_o()) {
                    swap(j, json_);
                    j.clear();
                } else
                    j = Json::make_object();
                goto value;
            } else
                goto error_here;

        case '}':
            if (state_ == st_object_initial || state_ == st_object_delim) {
                ++first;
                goto close_value;
            } else
                goto error_here;

        case '[':
            if (state_ <= st_object_value) {
                if (stack_.size() == max_depth)
                    goto error_here;
                ++first;
                if (state_ == st_initial && json_.is_a()) {
                    swap(j, json_);
                    j.clear();
                } else
                    j = Json::make_array();
                goto value;
            } else
                goto error_here;

        case ']':
            if (state_ == st_array_initial || state_ == st_array_delim) {
                ++first;
                goto close_value;
            } else
                goto error_here;

        case '\"':
            if (state_ <= st_object_value) {
                str_ = String();
                ++first;
            string_value:
                first = consume_string(first, last, str);
                if (state_ >= 0 && !(state_ & st_stringpart)) {
                    j = Json(std::move(str_));
                    goto value;
                }
            } else if (state_ == st_object_initial || state_ == st_object_key) {
                state_ = st_object_colon;
                str_ = String();
                ++first;
            string_object_key:
                first = consume_string(first, last, str);
                if (state_ >= 0 && !(state_ & st_stringpart)) {
                    stack_.push_back(&current()->get_insert(std::move(str_)));
                    continue;
                }
            } else
                goto error_here;
            break;

        case 'n':
        case 'f':
        case 't':
            if (state_ <= st_object_value) {
            primitive:
                first = consume_primitive(first, last, j);
                if (state_ >= 0 && !(state_ & st_primitivepart))
                    goto value;
            } else
                goto error_here;
            break;

        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            if (state_ <= st_object_value) {
            number:
                first = consume_number(first, last, str, complete, j);
                if (state_ >= 0 && !(state_ & st_numberpart))
                    goto value;
            } else
                goto error_here;
            break;

        default:
        error_here:
            return error_at(first);

        value: {
            Json* jp = current();
            if (state_ != st_initial && jp->is_a()) {
                jp->push_back(std::move(j));
                jp = &jp->back();
            } else
                swap(*jp, j);

            if (state_ == st_object_value)
                stack_.pop_back();
            if (jp->is_a() || jp->is_o()) {
                if (state_ != st_initial)
                    stack_.push_back(jp);
                state_ = jp->is_a() ? st_array_initial : st_object_initial;
            } else if (state_ == st_object_value)
                state_ = st_object_delim;
            else if (state_ == st_array_initial || state_ == st_array_value)
                state_ = st_array_delim;
            else {
                state_ = st_final;
                return first;
            }
            break;
        }

        close_value:
            if (stack_.empty()) {
                state_ = st_final;
                return first;
            } else {
                stack_.pop_back();
                state_ = current()->is_a() ? st_array_delim : st_object_delim;
            }
            break;
        }

    return first;
}

const uint8_t*
Json::streaming_parser::consume_string(const uint8_t* first,
                                       const uint8_t* last,
                                       const String& str) {
    StringAccum sa = StringAccum::make_transfer(str_);

    if (unlikely(state_ & st_partlenmask)) {
        first = consume_stringpart(sa, first, last);
        if (state_ == st_error)
            return first;
    }

    const uint8_t* prev = first;
    while (first != last) {
        if (likely(in_range(*first, 32, 128)) && *first != '\\' && *first != '\"')
            ++first;
        else if (*first == '\\') {
            sa.append(prev, first);
            prev = first = consume_backslash(sa, first, last);
            if (state_ == st_error)
                return first;
        } else if (*first == '\"')
            break;
        else if (unlikely(!in_range(*first, 0xC2, 0xF5)))
            return error_at(first);
        else {
            int want = 2 + (*first >= 0xE0) + (*first >= 0xF0);
            int n = last - first < want ? last - first : want;
            if ((n > 1 && unlikely(!in_range(first[1], 0x80, 0xC0)))
                || (*first >= 0xE0
                    && ((*first == 0xE0 && first[1] < 0xA0) /* overlong */
                        || (*first == 0xED && first[1] >= 0xA0) /* surrogate */
                        || (*first == 0xF0 && first[1] < 0x90) /* overlong */
                        || (*first == 0xF4 && first[1] >= 0x90) /* not a char */
                        )))
                return error_at(first + 1);
            else if (n > 2 && unlikely(!in_range(first[2], 0x80, 0xC0)))
                return error_at(first + 2);
            else if (n > 3 && unlikely(!in_range(first[3], 0x80, 0xC0)))
                return error_at(first + 3);
            first += n;
            if (n != want) {
                state_ |= n;
                break;
            }
        }
    }

    if (!sa.empty() || first == last)
        sa.append(prev, first);
    if (first != last) {
        if (!sa.empty())
            str_ = sa.take_string();
        else if (prev >= str.ubegin() && first <= str.uend())
            str_ = str.fast_substring(prev, first);
        else
            str_ = String(prev, first);
        state_ &= ~st_stringpart;
        return first + 1;
    } else {
        state_ |= st_stringpart;
        str_ = sa.take_string();
        return first;
    }
}

const uint8_t*
Json::streaming_parser::consume_stringpart(StringAccum& sa,
                                           const uint8_t* first,
                                           const uint8_t* last) {
    while ((state_ & st_partlenmask) && first != last) {
        int part = state_ & st_partlenmask;
        uint8_t tag = sa[sa.length() - part];
        if ((tag != '\\' && (*first & 0xC0) != 0x80)
            || (tag == '\\' && part == 6 && *first != '\\')
            || (tag == '\\' && part == 7 && *first != 'u')
            || (tag == '\\' && part != 1 && part != 6 && part != 7
                && !isxdigit(*first))) {
            state_ = st_error;
            break;
        }
        sa.append(*first);
        if ((tag != '\\' && part == 1 + (tag >= 0xE0) + (tag >= 0xF0))
            || (tag == '\\' && (part == 1 || part == 5 || part == 11))) {
            uint8_t buf[12];
            memcpy(buf, sa.end() - part - 1, part + 1);
            sa.adjust_length(-part - 1);
            state_ -= st_stringpart | part;
            str_ = sa.take_string();
            (void) consume_string(buf, buf + part + 1, String());
            if (state_ == st_error)
                break;
            sa = StringAccum::make_transfer(str_);
        } else
            ++state_;
        ++first;
    }
    return first;
}

const uint8_t*
Json::streaming_parser::consume_backslash(StringAccum& sa,
                                          const uint8_t* first,
                                          const uint8_t* last) {
    const uint8_t* prev = first;
    int ch = 0;

    if (first + 1 == last)
        goto incomplete;
    else if (first[1] == '\"' || first[1] == '\\' || first[1] == '/')
        ch = first[1];
    else if (first[1] == 'b')
        ch = '\b';
    else if (first[1] == 'f')
        ch = '\f';
    else if (first[1] == 'n')
        ch = '\n';
    else if (first[1] == 'r')
        ch = '\r';
    else if (first[1] == 't')
        ch = '\t';
    else if (first[1] == 'u') {
        for (int i = 2; i < 6; ++i) {
            if (first + i == last)
                goto incomplete;
            else if (in_range(first[i], '0', '9' + 1))
                ch = 16 * ch + first[i] - '0';
            else if (in_range(first[i], 'A', 'F' + 1))
                ch = 16 * ch + first[i] - 'A' + 10;
            else if (in_range(first[i], 'a', 'f' + 1))
                ch = 16 * ch + first[i] - 'a' + 10;
            else
                return error_at(&first[i]);
        }
        first += 4;
        // special handling required for surrogate pairs
        if (unlikely(in_range(ch, 0xD800, 0xE000))) {
            if (ch >= 0xDC00)
                return error_at(&first[1]);
            else if (first + 2 == last)
                goto incomplete;
            else if (first[2] != '\\')
                return error_at(&first[2]);
            else if (first + 3 == last)
                goto incomplete;
            else if (first[3] != 'u')
                return error_at(&first[3]);
            int ch2 = 0;
            for (int i = 4; i < 8; ++i) {
                if (first + i == last)
                    goto incomplete;
                else if (in_range(first[i], '0', '9' + 1))
                    ch2 = 16 * ch2 + first[i] - '0';
                else if (in_range(first[i], 'A', 'F' + 1))
                    ch2 = 16 * ch2 + first[i] - 'A' + 10;
                else if (in_range(first[i], 'A', 'F' + 1))
                    ch2 = 16 * ch2 + first[i] - 'a' + 10;
                else
                    return error_at(&first[i]);
            }
            if (!in_range(ch2, 0xDC00, 0xE000))
                return error_at(&first[7]);
            ch = 0x10000 + (ch - 0xD800) * 0x400 + (ch2 - 0xDC00);
            first += 6;
        }
    }

    if (!ch || !sa.append_utf8(ch))
        return error_at(&first[1]);
    return first + 2;

 incomplete:
    state_ |= last - prev;
    sa.append(prev, last);
    return last;
}

const uint8_t*
Json::streaming_parser::consume_primitive(const uint8_t* first,
                                          const uint8_t* last,
                                          Json& j) {
    const char* t = "null\0false\0true";
    int n;
    if (unlikely(state_ & st_primitivepart)) {
        n = state_ & st_partlenmask;
        state_ &= ~st_partmask;
    } else {
        n = (*first == 'n' ? 1 : (*first == 'f' ? 6 : 12));
        ++first;
    }

    for (; first != last && t[n]; ++n, ++first)
        if (t[n] != *first)
            return error_at(first);

    if (t[n])
        state_ |= st_primitivepart | n;
    else if (n == 4)
        j = Json();
    else if (n == 10)
        j = Json(false);
    else
        j = Json(true);
    return first;
}

const uint8_t*
Json::streaming_parser::consume_number(const uint8_t* first,
                                       const uint8_t* last,
                                       const String& str,
                                       bool complete,
                                       Json& j) {
    const uint8_t* prev = first;
    int position = state_ & st_partlenmask;

    switch (position) {
    case 0:
        if (*first == '-')
            ++first;
        /* fallthru */
    case 2:
        if (first != last && *first == '0') {
            position = 3;
            ++first;
        } else if (first != last && in_range(*first, '1', '9' + 1)) {
            position = 1;
            ++first;
        case 1:
            while (first != last && in_range(*first, '0', '9' + 1))
                ++first;
        } else
            position = 2;
        /* fallthru */
    case 3:
        if (first != last && *first == '.') {
            ++first;
            goto decimal;
        }
    maybe_exponent:
        if (first != last && (*first == 'e' || *first == 'E')) {
            ++first;
            goto exponent;
        }
        break;

    decimal:
    case 4:
        position = 4;
        if (first != last && in_range(*first, '0', '9' + 1)) {
            ++first;
            position = 5;
        }
        /* fallthru */
    case 5:
        while (first != last && in_range(*first, '0', '9' + 1))
            ++first;
        if (first != last && position == 5)
            goto maybe_exponent;
        break;

    exponent:
    case 6:
        position = 6;
        if (first != last && (*first == '+' || *first == '-'))
            ++first;
        else if (first == last)
            break;
        /* fallthru */
    case 8:
        position = 8;
        if (first != last && in_range(*first, '0', '9' + 1)) {
            ++first;
            position = 9;
        }
        /* fallthru */
    case 9:
        while (first != last && in_range(*first, '0', '9' + 1))
            ++first;
        break;
    }

    if (first != last || complete) {
        if (!(position & 1))
            goto error_here;
        last = first;
        if (state_ & st_partlenmask) {
            str_.append(prev, first);
            prev = str_.ubegin();
            first = str_.uend();
        }
        if (prev + 1 == first)
            j = Json(int(*prev - '0'));
        else if (position < 4) {
            bool negative = *prev == '-';
            prev += int(negative);
            uint64_t x = 0;
            while (prev != first) {
                x = (x * 10) + *prev - '0';
                ++prev;
            }
            if (negative)
                j = Json(-int64_t(x));
            else
                j = Json(x);
        } else {
            if (!(state_ & st_partlenmask))
                str_ = String(prev, first);
            double x = strtod(str_.c_str(), 0);
            j = Json(x);
        }
        state_ &= ~st_partmask;
        str_ = String();
        return last;
    }

    if (state_ & st_partmask)
        str_.append(prev, first);
    else if (prev >= str.ubegin() && first <= str.uend())
        str_ = str.substring(prev, first);
    else
        str_ = String(prev, first);
    state_ = (state_ & ~st_partmask) | st_numberpart | position;
    return first;

 error_here:
    str_ = String();
    return error_at(first);
}

} // namespace lcdf

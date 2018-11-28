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
#ifndef JSON_HH
#define JSON_HH
#include "straccum.hh"
#include "str.hh"
#include <vector>
#include <utility>
#include <stdlib.h>
namespace lcdf {

template <typename P> class Json_proxy_base;
template <typename T> class Json_object_proxy;
template <typename T> class Json_object_str_proxy;
template <typename T> class Json_array_proxy;
class Json_get_proxy;

template <typename T, size_t S = sizeof(String::rep_type) - sizeof(T)>
struct Json_rep_item;
template <typename T> struct Json_rep_item<T, 4> {
    T x;
    int type;
};
template <typename T> struct Json_rep_item<T, 8> {
    T x;
    int padding;
    int type;
};

class Json {
    enum json_type { // order matters
        j_string = -1, j_null = 0,
        j_array = 1, j_object = 2,
        j_int = 3, j_unsigned = 4, j_double = 5, j_bool = 6
    };

  public:
    struct null_t {
        inline constexpr null_t() { }
    };
    static const null_t null;
    static const Json null_json;

    typedef int size_type;

    typedef std::pair<const String, Json> object_value_type;
    class object_iterator;
    class const_object_iterator;

    typedef Json array_value_type;
    class array_iterator;
    class const_array_iterator;

    class iterator;
    class const_iterator;

    typedef bool (Json::*unspecified_bool_type)() const;
    class unparse_manipulator;
    class streaming_parser;

    // Constructors
    inline Json();
    inline Json(const Json& x);
    template <typename P> inline Json(const Json_proxy_base<P>& x);
#if HAVE_CXX_RVALUE_REFERENCES
    inline Json(Json&& x);
#endif
    inline Json(const null_t& x);
    inline Json(int x);
    inline Json(unsigned x);
    inline Json(long x);
    inline Json(unsigned long x);
    inline Json(long long x);
    inline Json(unsigned long long x);
    inline Json(double x);
    inline Json(bool x);
    inline Json(const String& x);
    inline Json(const std::string& x);
    inline Json(Str x);
    inline Json(const char* x);
    template <typename T> inline Json(const std::vector<T>& x);
    template <typename T> inline Json(T first, T last);
    inline ~Json();

    static inline const Json& make_null();
    static inline Json make_array();
    static inline Json make_array_reserve(int n);
    template <typename... Args>
    static inline Json array(Args&&... rest);
    static inline Json make_object();
    template <typename... Args>
    static inline Json object(Args&&... rest);
    static inline Json make_string(const String& x);
    static inline Json make_string(const std::string& x);
    static inline Json make_string(const char* s, int len);

    // Type information
    inline bool truthy() const;
    inline bool falsy() const;
    inline operator unspecified_bool_type() const;
    inline bool operator!() const;

    inline bool is_null() const;
    inline bool is_int() const;
    inline bool is_i() const;
    inline bool is_unsigned() const;
    inline bool is_u() const;
    inline bool is_signed() const;
    inline bool is_nonnegint() const;
    inline bool is_double() const;
    inline bool is_d() const;
    inline bool is_number() const;
    inline bool is_n() const;
    inline bool is_bool() const;
    inline bool is_b() const;
    inline bool is_string() const;
    inline bool is_s() const;
    inline bool is_array() const;
    inline bool is_a() const;
    inline bool is_object() const;
    inline bool is_o() const;
    inline bool is_primitive() const;

    inline bool empty() const;
    inline size_type size() const;
    inline bool shared() const;

    void clear();

    // Primitive extractors
    inline int64_t to_i() const;
    inline uint64_t to_u() const;
    inline uint64_t to_u64() const;
    inline bool to_i(int& x) const;
    inline bool to_i(unsigned& x) const;
    inline bool to_i(long& x) const;
    inline bool to_i(unsigned long& x) const;
    inline bool to_i(long long& x) const;
    inline bool to_i(unsigned long long& x) const;
    inline int64_t as_i() const;
    inline int64_t as_i(int64_t default_value) const;
    inline uint64_t as_u() const;
    inline uint64_t as_u(uint64_t default_value) const;

    inline double to_d() const;
    inline bool to_d(double& x) const;
    inline double as_d() const;
    inline double as_d(double default_value) const;

    inline bool to_b() const;
    inline bool to_b(bool& x) const;
    inline bool as_b() const;
    inline bool as_b(bool default_value) const;

    inline String to_s() const;
    inline bool to_s(Str& x) const;
    inline bool to_s(String& x) const;
    inline String& as_s();
    inline const String& as_s() const;
    inline const String& as_s(const String& default_value) const;

    // Object methods
    inline size_type count(Str key) const;
    inline const Json& get(Str key) const;
    inline Json& get_insert(const String& key);
    inline Json& get_insert(Str key);
    inline Json& get_insert(const char* key);

    inline long get_i(Str key) const;
    inline double get_d(Str key) const;
    inline bool get_b(Str key) const;
    inline String get_s(Str key) const;

    inline const Json_get_proxy get(Str key, Json& x) const;
    inline const Json_get_proxy get(Str key, int& x) const;
    inline const Json_get_proxy get(Str key, unsigned& x) const;
    inline const Json_get_proxy get(Str key, long& x) const;
    inline const Json_get_proxy get(Str key, unsigned long& x) const;
    inline const Json_get_proxy get(Str key, long long& x) const;
    inline const Json_get_proxy get(Str key, unsigned long long& x) const;
    inline const Json_get_proxy get(Str key, double& x) const;
    inline const Json_get_proxy get(Str key, bool& x) const;
    inline const Json_get_proxy get(Str key, Str& x) const;
    inline const Json_get_proxy get(Str key, String& x) const;

    const Json& operator[](Str key) const;
    inline Json_object_proxy<Json> operator[](const String& key);
    inline Json_object_str_proxy<Json> operator[](const std::string& key);
    inline Json_object_str_proxy<Json> operator[](Str key);
    inline Json_object_str_proxy<Json> operator[](const char* key);

    inline const Json& at(Str key) const;
    inline Json& at_insert(const String& key);
    inline Json& at_insert(Str key);
    inline Json& at_insert(const char* key);

    inline Json& set(const String& key, Json value);
    template <typename P>
    inline Json& set(const String& key, const Json_proxy_base<P>& value);
    inline Json& unset(Str key);

    inline Json& set_list();
    template <typename T, typename... Args>
    inline Json& set_list(const String& key, T value, Args&&... rest);

    inline std::pair<object_iterator, bool> insert(const object_value_type& x);
    inline object_iterator insert(object_iterator position,
                                  const object_value_type& x);
    inline object_iterator erase(object_iterator it);
    inline size_type erase(Str key);

    inline Json& merge(const Json& x);
    template <typename P> inline Json& merge(const Json_proxy_base<P>& x);

    // Array methods
    inline const Json& get(size_type x) const;
    inline Json& get_insert(size_type x);

    inline const Json& operator[](size_type x) const;
    inline Json_array_proxy<Json> operator[](size_type x);

    inline const Json& at(size_type x) const;
    inline Json& at_insert(size_type x);

    inline const Json& back() const;
    inline Json& back();

    inline Json& push_back(Json x);
    template <typename P> inline Json& push_back(const Json_proxy_base<P>& x);
    inline void pop_back();

    inline Json& push_back_list();
    template <typename T, typename... Args>
    inline Json& push_back_list(T first, Args&&... rest);

    inline array_iterator insert(array_iterator position, Json x);
    template <typename P>
    inline array_iterator insert(array_iterator position, const Json_proxy_base<P>& x);
    array_iterator erase(array_iterator first, array_iterator last);
    inline array_iterator erase(array_iterator position);

    void reserve(size_type n);
    void resize(size_type n);

    inline Json* array_data();
    inline const Json* array_data() const;
    inline const Json* array_cdata() const;
    inline Json* end_array_data();
    inline const Json* end_array_data() const;
    inline const Json* end_array_cdata() const;

    // Iteration
    inline const_object_iterator obegin() const;
    inline const_object_iterator oend() const;
    inline object_iterator obegin();
    inline object_iterator oend();
    inline const_object_iterator cobegin() const;
    inline const_object_iterator coend() const;

    inline const_array_iterator abegin() const;
    inline const_array_iterator aend() const;
    inline array_iterator abegin();
    inline array_iterator aend();
    inline const_array_iterator cabegin() const;
    inline const_array_iterator caend() const;

    inline const_iterator begin() const;
    inline const_iterator end() const;
    inline iterator begin();
    inline iterator end();
    inline const_iterator cbegin() const;
    inline const_iterator cend() const;

    // Unparsing
    static inline unparse_manipulator indent_depth(int x);
    static inline unparse_manipulator tab_width(int x);
    static inline unparse_manipulator newline_terminator(bool x);
    static inline unparse_manipulator space_separator(bool x);

    inline String unparse() const;
    inline String unparse(const unparse_manipulator& m) const;
    inline void unparse(StringAccum& sa) const;
    inline void unparse(StringAccum& sa, const unparse_manipulator& m) const;

    // Parsing
    inline bool assign_parse(const String& str);
    inline bool assign_parse(const char* first, const char* last);

    static inline Json parse(const String& str);
    static inline Json parse(const char* first, const char* last);

    // Assignment
    inline Json& operator=(const Json& x);
#if HAVE_CXX_RVALUE_REFERENCES
    inline Json& operator=(Json&& x);
#endif
    inline Json& operator=(int x);
    inline Json& operator=(unsigned x);
    inline Json& operator=(long x);
    inline Json& operator=(unsigned long x);
    inline Json& operator=(long long x);
    inline Json& operator=(unsigned long long x);
    inline Json& operator=(double x);
    inline Json& operator=(bool x);
    inline Json& operator=(const String& x);
    template <typename P> inline Json& operator=(const Json_proxy_base<P>& x);

    inline Json& operator++();
    inline void operator++(int);
    inline Json& operator--();
    inline void operator--(int);
    inline Json& operator+=(int x);
    inline Json& operator+=(unsigned x);
    inline Json& operator+=(long x);
    inline Json& operator+=(unsigned long x);
    inline Json& operator+=(long long x);
    inline Json& operator+=(unsigned long long x);
    inline Json& operator+=(double x);
    inline Json& operator+=(const Json& x);
    inline Json& operator-=(int x);
    inline Json& operator-=(unsigned x);
    inline Json& operator-=(long x);
    inline Json& operator-=(unsigned long x);
    inline Json& operator-=(long long x);
    inline Json& operator-=(unsigned long long x);
    inline Json& operator-=(double x);
    inline Json& operator-=(const Json& x);

    friend bool operator==(const Json& a, const Json& b);

    inline void swap(Json& x);

  private:
    enum {
        st_initial = 0, st_array_initial = 1, st_array_delim = 2,
        st_array_value = 3, st_object_initial = 4, st_object_delim = 5,
        st_object_key = 6, st_object_colon = 7, st_object_value = 8,
        max_depth = 2048
    };

    struct ComplexJson {
        int refcount;
        int size;
        ComplexJson()
            : refcount(1) {
        }
        inline void ref();
        inline void deref(json_type j);
      private:
        ComplexJson(const ComplexJson& x); // does not exist
    };

    struct ArrayJson;
    struct ObjectItem;
    struct ObjectJson;

    union rep_type {
        Json_rep_item<int64_t> i;
        Json_rep_item<uint64_t> u;
        Json_rep_item<double> d;
        String::rep_type str;
        Json_rep_item<ArrayJson*> a;
        Json_rep_item<ObjectJson*> o;
        Json_rep_item<ComplexJson*> x;
    } u_;

    inline void deref();

    inline ObjectJson* ojson() const;
    inline ArrayJson* ajson() const;

    int64_t hard_to_i() const;
    uint64_t hard_to_u() const;
    double hard_to_d() const;
    bool hard_to_b() const;
    String hard_to_s() const;
    inline void force_number();
    inline void force_double();
    inline Json& add(double x);
    template <typename T> inline Json& add(T x);
    inline Json& subtract(double x);
    template <typename T> inline Json& subtract(T x);

    const Json& hard_get(Str key) const;
    const Json& hard_get(size_type x) const;
    Json& hard_get_insert(size_type x);

    inline void uniqueify_object(bool convert);
    void hard_uniqueify_object(bool convert);
    inline void uniqueify_array(bool convert, int ncap);
    void hard_uniqueify_array(bool convert, int ncap);
    void* uniqueify_array_insert(bool convert, size_type pos);

    static unparse_manipulator default_manipulator;
    bool unparse_is_complex() const;
    static void unparse_indent(StringAccum &sa, const unparse_manipulator &m, int depth);
    void hard_unparse(StringAccum &sa, const unparse_manipulator &m, int depth) const;

    bool assign_parse(const char* first, const char* last, const String &str);

    friend class object_iterator;
    friend class const_object_iterator;
    friend class array_iterator;
    friend class const_array_iterator;
    friend Json operator+(Json);
    friend Json operator-(Json);
};


struct Json::ArrayJson : public ComplexJson {
    int capacity;
    Json a[0];

    inline ArrayJson(int cap)
        : capacity(cap) {
        size = 0;
    }
    static ArrayJson* make(int n);
    static void destroy(ArrayJson* a);
};

struct Json::ObjectItem {
    std::pair<const String, Json> v_;
    int next_;
    explicit ObjectItem(const String &key, const Json& value, int next)
        : v_(key, value), next_(next) {
    }
};

struct Json::ObjectJson : public ComplexJson {
    ObjectItem *os_;
    int n_;
    int capacity_;
    std::vector<int> hash_;
    ObjectJson()
        : os_(), n_(0), capacity_(0) {
        size = 0;
    }
    ObjectJson(const ObjectJson& x);
    ~ObjectJson();
    void grow(bool copy);
    int bucket(const char* s, int len) const {
        return String::hashcode(s, s + len) & (hash_.size() - 1);
    }
    ObjectItem& item(int p) const {
        return os_[p];
    }
    int find(const char* s, int len) const {
        if (hash_.size()) {
            int p = hash_[bucket(s, len)];
            while (p >= 0) {
                ObjectItem &oi = item(p);
                if (oi.v_.first.equals(s, len))
                    return p;
                p = oi.next_;
            }
        }
        return -1;
    }
    int find_insert(const String& key, const Json& value);
    inline Json& get_insert(const String& key) {
        int p = find_insert(key, make_null());
        return item(p).v_.second;
    }
    Json& get_insert(Str key);
    void erase(int p);
    size_type erase(Str key);
    void rehash();
};

inline const Json& Json::make_null() {
    return null_json;
}

inline void Json::ComplexJson::ref() {
    if (refcount >= 0)
        ++refcount;
}

inline void Json::ComplexJson::deref(json_type j) {
    if (refcount >= 1 && --refcount == 0) {
        if (j == j_object)
            delete static_cast<ObjectJson*>(this);
        else
            ArrayJson::destroy(static_cast<ArrayJson*>(this));
    }
}

inline Json::ArrayJson* Json::ajson() const {
    precondition(u_.x.type == j_null || u_.x.type == j_array);
    return u_.a.x;
}

inline Json::ObjectJson* Json::ojson() const {
    precondition(u_.x.type == j_null || u_.x.type == j_object);
    return u_.o.x;
}

inline void Json::uniqueify_array(bool convert, int ncap) {
    if (u_.x.type != j_array || !u_.a.x || u_.a.x->refcount != 1
        || (ncap > 0 && ncap > u_.a.x->capacity))
        hard_uniqueify_array(convert, ncap);
}

inline void Json::uniqueify_object(bool convert) {
    if (u_.x.type != j_object || !u_.o.x || u_.o.x->refcount != 1)
        hard_uniqueify_object(convert);
}


class Json::const_object_iterator { public:
    typedef std::pair<const String, Json> value_type;
    typedef const value_type* pointer_type;
    typedef const value_type& reference_type;
    typedef std::forward_iterator_tag iterator_category;

    const_object_iterator() {
    }
    typedef bool (const_object_iterator::*unspecified_bool_type)() const;
    operator unspecified_bool_type() const {
        return live() ? &const_object_iterator::live : 0;
    }
    bool live() const {
        return i_ >= 0;
    }
    const value_type& operator*() const {
        return j_->ojson()->item(i_).v_;
    }
    const value_type* operator->() const {
        return &(**this);
    }
    const String& key() const {
        return (**this).first;
    }
    const Json& value() const {
        return (**this).second;
    }
    void operator++() {
        ++i_;
        fix();
    }
    void operator++(int) {
        ++(*this);
    }
  private:
    const Json* j_;
    int i_;
    const_object_iterator(const Json* j, int i)
        : j_(j), i_(i) {
        if (i_ >= 0)
            fix();
    }
    void fix() {
        ObjectJson* oj = j_->ojson();
    retry:
        if (!oj || i_ >= oj->n_)
            i_ = -1;
        else if (oj->item(i_).next_ == -2) {
            ++i_;
            goto retry;
        }
    }
    friend class Json;
    friend bool operator==(const const_object_iterator&, const const_object_iterator&);
};

class Json::object_iterator : public const_object_iterator { public:
    typedef value_type* pointer_type;
    typedef value_type& reference_type;

    object_iterator() {
    }
    value_type& operator*() const {
        const_cast<Json*>(j_)->uniqueify_object(false);
        return j_->ojson()->item(i_).v_;
    }
    value_type* operator->() const {
        return &(**this);
    }
    Json& value() const {
        return (**this).second;
    }
  private:
    object_iterator(Json* j, int i)
        : const_object_iterator(j, i) {
    }
    friend class Json;
};

inline bool operator==(const Json::const_object_iterator& a, const Json::const_object_iterator& b) {
    return a.j_ == b.j_ && a.i_ == b.i_;
}

inline bool operator!=(const Json::const_object_iterator& a, const Json::const_object_iterator& b) {
    return !(a == b);
}

class Json::const_array_iterator { public:
    typedef Json::size_type difference_type;
    typedef Json value_type;
    typedef const Json* pointer;
    typedef const Json& reference;
    typedef std::random_access_iterator_tag iterator_category;

    const_array_iterator() {
    }
    typedef bool (const_array_iterator::*unspecified_bool_type)() const;
    operator unspecified_bool_type() const {
        return live() ? &const_array_iterator::live : 0;
    }
    bool live() const {
        ArrayJson* aj = j_->ajson();
        return aj && i_ < aj->size;
    }
    const Json& operator*() const {
        return j_->ajson()->a[i_];
    }
    const Json& operator[](difference_type i) const {
        return j_->ajson()->a[i_ + i];
    }
    const Json* operator->() const {
        return &(**this);
    }
    const Json& value() const {
        return **this;
    }
    void operator++(int) {
        ++i_;
    }
    const_array_iterator& operator++() {
        ++i_;
        return *this;
    }
    void operator--(int) {
        --i_;
    }
    const_array_iterator& operator--() {
        --i_;
        return *this;
    }
    const_array_iterator& operator+=(difference_type x) {
        i_ += x;
        return *this;
    }
    const_array_iterator& operator-=(difference_type x) {
        i_ -= x;
        return *this;
    }
  private:
    const Json* j_;
    int i_;
    const_array_iterator(const Json* j, int i)
        : j_(j), i_(i) {
    }
    friend class Json;
    friend class Json::array_iterator;
    friend bool operator==(const const_array_iterator&, const const_array_iterator&);
    friend bool operator<(const const_array_iterator&, const const_array_iterator&);
    friend difference_type operator-(const const_array_iterator&, const const_array_iterator&);
};

class Json::array_iterator : public const_array_iterator { public:
    typedef const Json* pointer;
    typedef const Json& reference;

    array_iterator() {
    }
    Json& operator*() const {
        const_cast<Json*>(j_)->uniqueify_array(false, 0);
        return j_->ajson()->a[i_];
    }
    Json& operator[](difference_type i) const {
        const_cast<Json*>(j_)->uniqueify_array(false, 0);
        return j_->ajson()->a[i_ + i];
    }
    Json* operator->() const {
        return &(**this);
    }
    Json& value() const {
        return **this;
    }
    array_iterator& operator++() {
        ++i_;
        return *this;
    }
    array_iterator& operator--() {
        --i_;
        return *this;
    }
    array_iterator& operator+=(difference_type x) {
        i_ += x;
        return *this;
    }
    array_iterator& operator-=(difference_type x) {
        i_ -= x;
        return *this;
    }
  private:
    array_iterator(Json* j, int i)
        : const_array_iterator(j, i) {
    }
    friend class Json;
};

inline bool operator==(const Json::const_array_iterator& a, const Json::const_array_iterator& b) {
    return a.j_ == b.j_ && a.i_ == b.i_;
}

inline bool operator<(const Json::const_array_iterator& a, const Json::const_array_iterator& b) {
    return a.j_ < b.j_ || (a.j_ == b.j_ && a.i_ < b.i_);
}

inline bool operator!=(const Json::const_array_iterator& a, const Json::const_array_iterator& b) {
    return !(a == b);
}

inline bool operator<=(const Json::const_array_iterator& a, const Json::const_array_iterator& b) {
    return !(b < a);
}

inline bool operator>(const Json::const_array_iterator& a, const Json::const_array_iterator& b) {
    return b < a;
}

inline bool operator>=(const Json::const_array_iterator& a, const Json::const_array_iterator& b) {
    return !(a < b);
}

inline Json::const_array_iterator operator+(Json::const_array_iterator a, Json::const_array_iterator::difference_type i) {
    return a += i;
}
inline Json::array_iterator operator+(Json::array_iterator a, Json::array_iterator::difference_type i) {
    return a += i;
}

inline Json::const_array_iterator operator-(Json::const_array_iterator a, Json::const_array_iterator::difference_type i) {
    return a -= i;
}
inline Json::array_iterator operator-(Json::array_iterator a, Json::array_iterator::difference_type i) {
    return a -= i;
}

inline Json::const_array_iterator::difference_type operator-(const Json::const_array_iterator& a, const Json::const_array_iterator& b) {
    precondition(a.j_ == b.j_);
    return a.i_ - b.i_;
}

class Json::const_iterator { public:
    typedef std::pair<const String, Json&> value_type;
    typedef const value_type* pointer;
    typedef const value_type& reference;
    typedef std::forward_iterator_tag iterator_category;

    const_iterator()
        : value_(String(), *(Json*) 0) {
    }
    typedef bool (const_iterator::*unspecified_bool_type)() const;
    operator unspecified_bool_type() const {
        return live() ? &const_iterator::live : 0;
    }
    bool live() const {
        return i_ >= 0;
    }
    const value_type& operator*() const {
        return value_;
    }
    const value_type* operator->() const {
        return &(**this);
    }
    const String& key() const {
        return (**this).first;
    }
    const Json& value() const {
        return (**this).second;
    }
    void operator++() {
        ++i_;
        fix();
    }
    void operator++(int) {
        ++(*this);
    }
  private:
    const Json* j_;
    int i_;
    value_type value_;
    const_iterator(const Json* j, int i)
        : j_(j), i_(i), value_(String(), *(Json*) 0) {
        if (i_ >= 0)
            fix();
    }
    void fix() {
        if (j_->u_.x.type == j_object) {
            ObjectJson* oj = j_->ojson();
        retry:
            if (!oj || i_ >= oj->n_)
                i_ = -1;
            else if (oj->item(i_).next_ == -2) {
                ++i_;
                goto retry;
            } else {
                value_.~pair();
                new((void *) &value_) value_type(oj->item(i_).v_.first,
                                                 oj->item(i_).v_.second);
            }
        } else {
            ArrayJson *aj = j_->ajson();
            if (!aj || unsigned(i_) >= unsigned(aj->size))
                i_ = -1;
            else {
                value_.~pair();
                new((void *) &value_) value_type(String(i_), aj->a[i_]);
            }
        }
    }
    friend class Json;
    friend bool operator==(const const_iterator &, const const_iterator &);
};

class Json::iterator : public const_iterator { public:
    typedef value_type* pointer;
    typedef value_type& reference;

    iterator() {
    }
    value_type& operator*() const {
        if (j_->u_.x.x->refcount != 1)
            uniqueify();
        return const_cast<value_type&>(const_iterator::operator*());
    }
    value_type* operator->() const {
        return &(**this);
    }
    Json& value() const {
        return (**this).second;
    }
  private:
    iterator(Json *j, int i)
        : const_iterator(j, i) {
    }
    void uniqueify() const {
        if (j_->u_.x.type == j_object)
            const_cast<Json*>(j_)->hard_uniqueify_object(false);
        else
            const_cast<Json*>(j_)->hard_uniqueify_array(false, 0);
        const_cast<iterator*>(this)->fix();
    }
    friend class Json;
};

inline bool operator==(const Json::const_iterator& a, const Json::const_iterator& b) {
    return a.j_ == b.j_ && a.i_ == b.i_;
}

inline bool operator!=(const Json::const_iterator& a, const Json::const_iterator& b) {
    return !(a == b);
}


template <typename P>
class Json_proxy_base {
  public:
    const Json& cvalue() const {
        return static_cast<const P *>(this)->cvalue();
    }
    Json& value() {
        return static_cast<P *>(this)->value();
    }
    operator const Json&() const {
        return cvalue();
    }
    operator Json&() {
        return value();
    }
    bool truthy() const {
        return cvalue().truthy();
    }
    bool falsy() const {
        return cvalue().falsy();
    }
    operator Json::unspecified_bool_type() const {
        return cvalue();
    }
    bool operator!() const {
        return !cvalue();
    }
    bool is_null() const {
        return cvalue().is_null();
    }
    bool is_int() const {
        return cvalue().is_int();
    }
    bool is_i() const {
        return cvalue().is_i();
    }
    bool is_unsigned() const {
        return cvalue().is_unsigned();
    }
    bool is_u() const {
        return cvalue().is_u();
    }
    bool is_signed() const {
        return cvalue().is_signed();
    }
    bool is_nonnegint() const {
        return cvalue().is_nonnegint();
    }
    bool is_double() const {
        return cvalue().is_double();
    }
    bool is_d() const {
        return cvalue().is_d();
    }
    bool is_number() const {
        return cvalue().is_number();
    }
    bool is_n() const {
        return cvalue().is_n();
    }
    bool is_bool() const {
        return cvalue().is_bool();
    }
    bool is_b() const {
        return cvalue().is_b();
    }
    bool is_string() const {
        return cvalue().is_string();
    }
    bool is_s() const {
        return cvalue().is_s();
    }
    bool is_array() const {
        return cvalue().is_array();
    }
    bool is_a() const {
        return cvalue().is_a();
    }
    bool is_object() const {
        return cvalue().is_object();
    }
    bool is_o() const {
        return cvalue().is_o();
    }
    bool is_primitive() const {
        return cvalue().is_primitive();
    }
    bool empty() const {
        return cvalue().empty();
    }
    Json::size_type size() const {
        return cvalue().size();
    }
    int64_t to_i() const {
        return cvalue().to_i();
    }
    uint64_t to_u() const {
        return cvalue().to_u();
    }
    uint64_t to_u64() const {
        return cvalue().to_u64();
    }
    bool to_i(int& x) const {
        return cvalue().to_i(x);
    }
    bool to_i(unsigned& x) const {
        return cvalue().to_i(x);
    }
    bool to_i(long& x) const {
        return cvalue().to_i(x);
    }
    bool to_i(unsigned long& x) const {
        return cvalue().to_i(x);
    }
    bool to_i(long long& x) const {
        return cvalue().to_i(x);
    }
    bool to_i(unsigned long long& x) const {
        return cvalue().to_i(x);
    }
    int64_t as_i() const {
        return cvalue().as_i();
    }
    int64_t as_i(int64_t default_value) const {
        return cvalue().as_i(default_value);
    }
    uint64_t as_u() const {
        return cvalue().as_u();
    }
    uint64_t as_u(uint64_t default_value) const {
        return cvalue().as_u(default_value);
    }
    double to_d() const {
        return cvalue().to_d();
    }
    bool to_d(double& x) const {
        return cvalue().to_d(x);
    }
    double as_d() const {
        return cvalue().as_d();
    }
    double as_d(double default_value) const {
        return cvalue().as_d(default_value);
    }
    bool to_b() const {
        return cvalue().to_b();
    }
    bool to_b(bool& x) const {
        return cvalue().to_b(x);
    }
    bool as_b() const {
        return cvalue().as_b();
    }
    bool as_b(bool default_value) const {
        return cvalue().as_b(default_value);
    }
    String to_s() const {
        return cvalue().to_s();
    }
    bool to_s(Str& x) const {
        return cvalue().to_s(x);
    }
    bool to_s(String& x) const {
        return cvalue().to_s(x);
    }
    const String& as_s() const {
        return cvalue().as_s();
    }
    const String& as_s(const String& default_value) const {
        return cvalue().as_s(default_value);
    }
    Json::size_type count(Str key) const {
        return cvalue().count(key);
    }
    const Json& get(Str key) const {
        return cvalue().get(key);
    }
    Json& get_insert(const String& key) {
        return value().get_insert(key);
    }
    Json& get_insert(Str key) {
        return value().get_insert(key);
    }
    Json& get_insert(const char* key) {
        return value().get_insert(key);
    }
    long get_i(Str key) const {
        return cvalue().get_i(key);
    }
    double get_d(Str key) const {
        return cvalue().get_d(key);
    }
    bool get_b(Str key) const {
        return cvalue().get_b(key);
    }
    String get_s(Str key) const {
        return cvalue().get_s(key);
    }
    inline const Json_get_proxy get(Str key, Json& x) const;
    inline const Json_get_proxy get(Str key, int& x) const;
    inline const Json_get_proxy get(Str key, unsigned& x) const;
    inline const Json_get_proxy get(Str key, long& x) const;
    inline const Json_get_proxy get(Str key, unsigned long& x) const;
    inline const Json_get_proxy get(Str key, long long& x) const;
    inline const Json_get_proxy get(Str key, unsigned long long& x) const;
    inline const Json_get_proxy get(Str key, double& x) const;
    inline const Json_get_proxy get(Str key, bool& x) const;
    inline const Json_get_proxy get(Str key, Str& x) const;
    inline const Json_get_proxy get(Str key, String& x) const;
    const Json& operator[](Str key) const {
        return cvalue().get(key);
    }
    Json_object_proxy<P> operator[](const String& key) {
        return Json_object_proxy<P>(*static_cast<P*>(this), key);
    }
    Json_object_str_proxy<P> operator[](std::string key) {
        return Json_object_str_proxy<P>(*static_cast<P*>(this), Str(key.data(), key.length()));
    }
    Json_object_str_proxy<P> operator[](Str key) {
        return Json_object_str_proxy<P>(*static_cast<P*>(this), key);
    }
    Json_object_str_proxy<P> operator[](const char* key) {
        return Json_object_str_proxy<P>(*static_cast<P*>(this), key);
    }
    const Json& at(Str key) const {
        return cvalue().at(key);
    }
    Json& at_insert(const String& key) {
        return value().at_insert(key);
    }
    Json& at_insert(Str key) {
        return value().at_insert(key);
    }
    Json& at_insert(const char* key) {
        return value().at_insert(key);
    }
    inline Json& set(const String& key, Json value) {
        return this->value().set(key, value);
    }
    template <typename Q>
    inline Json& set(const String& key, const Json_proxy_base<Q>& value) {
        return this->value().set(key, value);
    }
    Json& unset(Str key) {
        return value().unset(key);
    }
    template <typename... Args>
    inline Json& set_list(Args&&... args) {
        return value().set_list(std::forward<Args>(args)...);
    }
    std::pair<Json::object_iterator, bool> insert(const Json::object_value_type &x) {
        return value().insert(x);
    }
    Json::object_iterator insert(Json::object_iterator position, const Json::object_value_type &x) {
        return value().insert(position, x);
    }
    Json::object_iterator erase(Json::object_iterator it) {
        return value().erase(it);
    }
    Json::size_type erase(Str key) {
        return value().erase(key);
    }
    Json& merge(const Json& x) {
        return value().merge(x);
    }
    template <typename P2> Json& merge(const Json_proxy_base<P2>& x) {
        return value().merge(x.cvalue());
    }
    const Json& get(Json::size_type x) const {
        return cvalue().get(x);
    }
    Json& get_insert(Json::size_type x) {
        return value().get_insert(x);
    }
    const Json& operator[](int key) const {
        return cvalue().at(key);
    }
    Json_array_proxy<P> operator[](int key) {
        return Json_array_proxy<P>(*static_cast<P*>(this), key);
    }
    const Json& at(Json::size_type x) const {
        return cvalue().at(x);
    }
    Json& at_insert(Json::size_type x) {
        return value().at_insert(x);
    }
    const Json& back() const {
        return cvalue().back();
    }
    Json& back() {
        return value().back();
    }
    Json& push_back(Json x) {
        return value().push_back(std::move(x));
    }
    template <typename Q> Json& push_back(const Json_proxy_base<Q>& x) {
        return value().push_back(x);
    }
    void pop_back() {
        value().pop_back();
    }
    template <typename... Args> Json& push_back_list(Args&&... args) {
        return value().push_back_list(std::forward<Args>(args)...);
    }
    Json::array_iterator insert(Json::array_iterator position, Json x) {
        return value().insert(position, std::move(x));
    }
    template <typename Q> Json::array_iterator insert(Json::array_iterator position, const Json_proxy_base<Q>& x) {
        return value().insert(position, x);
    }
    Json::array_iterator erase(Json::array_iterator first, Json::array_iterator last) {
        return value().erase(first, last);
    }
    Json::array_iterator erase(Json::array_iterator position) {
        return value().erase(position);
    }
    void resize(Json::size_type n) {
        value().resize(n);
    }
    void unparse(StringAccum& sa) const {
        return cvalue().unparse(sa);
    }
    void unparse(StringAccum& sa, const Json::unparse_manipulator& m) const {
        return cvalue().unparse(sa, m);
    }
    String unparse() const {
        return cvalue().unparse();
    }
    String unparse(const Json::unparse_manipulator& m) const {
        return cvalue().unparse(m);
    }
    bool assign_parse(const String& str) {
        return value().assign_parse(str);
    }
    bool assign_parse(const char* first, const char* last) {
        return value().assign_parse(first, last);
    }
    void swap(Json& x) {
        value().swap(x);
    }
    Json& operator++() {
        return ++value();
    }
    void operator++(int) {
        value()++;
    }
    Json& operator--() {
        return --value();
    }
    void operator--(int) {
        value()--;
    }
    Json& operator+=(int x) {
        return value() += x;
    }
    Json& operator+=(unsigned x) {
        return value() += x;
    }
    Json& operator+=(long x) {
        return value() += x;
    }
    Json& operator+=(unsigned long x) {
        return value() += x;
    }
    Json& operator+=(long long x) {
        return value() += x;
    }
    Json& operator+=(unsigned long long x) {
        return value() += x;
    }
    Json& operator+=(double x) {
        return value() += x;
    }
    Json& operator+=(const Json& x) {
        return value() += x;
    }
    Json& operator-=(int x) {
        return value() -= x;
    }
    Json& operator-=(unsigned x) {
        return value() -= x;
    }
    Json& operator-=(long x) {
        return value() -= x;
    }
    Json& operator-=(unsigned long x) {
        return value() -= x;
    }
    Json& operator-=(long long x) {
        return value() -= x;
    }
    Json& operator-=(unsigned long long x) {
        return value() -= x;
    }
    Json& operator-=(double x) {
        return value() -= x;
    }
    Json& operator-=(const Json& x) {
        return value() -= x;
    }
    Json::const_object_iterator obegin() const {
        return cvalue().obegin();
    }
    Json::const_object_iterator oend() const {
        return cvalue().oend();
    }
    Json::object_iterator obegin() {
        return value().obegin();
    }
    Json::object_iterator oend() {
        return value().oend();
    }
    Json::const_object_iterator cobegin() const {
        return cvalue().cobegin();
    }
    Json::const_object_iterator coend() const {
        return cvalue().coend();
    }
    Json::const_array_iterator abegin() const {
        return cvalue().abegin();
    }
    Json::const_array_iterator aend() const {
        return cvalue().aend();
    }
    Json::array_iterator abegin() {
        return value().abegin();
    }
    Json::array_iterator aend() {
        return value().aend();
    }
    Json::const_array_iterator cabegin() const {
        return cvalue().cabegin();
    }
    Json::const_array_iterator caend() const {
        return cvalue().caend();
    }
    Json::const_iterator begin() const {
        return cvalue().begin();
    }
    Json::const_iterator end() const {
        return cvalue().end();
    }
    Json::iterator begin() {
        return value().begin();
    }
    Json::iterator end() {
        return value().end();
    }
    Json::const_iterator cbegin() const {
        return cvalue().cbegin();
    }
    Json::const_iterator cend() const {
        return cvalue().cend();
    }
};

template <typename T>
class Json_object_proxy : public Json_proxy_base<Json_object_proxy<T> > {
  public:
    const Json& cvalue() const {
        return base_.get(key_);
    }
    Json& value() {
        return base_.get_insert(key_);
    }
    Json& operator=(const Json& x) {
        return value() = x;
    }
#if HAVE_CXX_RVALUE_REFERENCES
    Json& operator=(Json&& x) {
        return value() = std::move(x);
    }
#endif
    Json& operator=(const Json_object_proxy<T>& x) {
        return value() = x.cvalue();
    }
    template <typename P> Json& operator=(const Json_proxy_base<P>& x) {
        return value() = x.cvalue();
    }
    Json_object_proxy(T& ref, const String& key)
        : base_(ref), key_(key) {
    }
    T &base_;
    String key_;
};

template <typename T>
class Json_object_str_proxy : public Json_proxy_base<Json_object_str_proxy<T> > {
  public:
    const Json& cvalue() const {
        return base_.get(key_);
    }
    Json& value() {
        return base_.get_insert(key_);
    }
    Json& operator=(const Json& x) {
        return value() = x;
    }
#if HAVE_CXX_RVALUE_REFERENCES
    Json& operator=(Json&& x) {
        return value() = std::move(x);
    }
#endif
    Json& operator=(const Json_object_str_proxy<T>& x) {
        return value() = x.cvalue();
    }
    template <typename P> Json& operator=(const Json_proxy_base<P>& x) {
        return value() = x.cvalue();
    }
    Json_object_str_proxy(T& ref, Str key)
        : base_(ref), key_(key) {
    }
    T &base_;
    Str key_;
};

template <typename T>
class Json_array_proxy : public Json_proxy_base<Json_array_proxy<T> > {
  public:
    const Json& cvalue() const {
        return base_.get(key_);
    }
    Json& value() {
        return base_.get_insert(key_);
    }
    Json& operator=(const Json& x) {
        return value() = x;
    }
#if HAVE_CXX_RVALUE_REFERENCES
    Json& operator=(Json&& x) {
        return value() = std::move(x);
    }
#endif
    Json& operator=(const Json_array_proxy<T>& x) {
        return value() = x.cvalue();
    }
    template <typename P> Json& operator=(const Json_proxy_base<P>& x) {
        return value() = x.cvalue();
    }
    Json_array_proxy(T& ref, int key)
        : base_(ref), key_(key) {
    }
    T &base_;
    int key_;
};

class Json_get_proxy : public Json_proxy_base<Json_get_proxy> {
  public:
    const Json& cvalue() const {
        return base_;
    }
    operator Json::unspecified_bool_type() const {
        return status_ ? &Json::is_null : 0;
    }
    bool operator!() const {
        return !status_;
    }
    bool status() const {
        return status_;
    }
    const Json_get_proxy& status(bool& x) const {
        x = status_;
        return *this;
    }
    Json_get_proxy(const Json& ref, bool status)
        : base_(ref), status_(status) {
    }
    const Json& base_;
    bool status_;
  private:
    Json_get_proxy& operator=(const Json_get_proxy& x);
};

template <typename T>
inline const Json_get_proxy Json_proxy_base<T>::get(Str key, Json& x) const {
    return cvalue().get(key, x);
}
template <typename T>
inline const Json_get_proxy Json_proxy_base<T>::get(Str key, int& x) const {
    return cvalue().get(key, x);
}
template <typename T>
inline const Json_get_proxy Json_proxy_base<T>::get(Str key, unsigned& x) const {
    return cvalue().get(key, x);
}
template <typename T>
inline const Json_get_proxy Json_proxy_base<T>::get(Str key, long& x) const {
    return cvalue().get(key, x);
}
template <typename T>
inline const Json_get_proxy Json_proxy_base<T>::get(Str key, unsigned long& x) const {
    return cvalue().get(key, x);
}
template <typename T>
inline const Json_get_proxy Json_proxy_base<T>::get(Str key, long long& x) const {
    return cvalue().get(key, x);
}
template <typename T>
inline const Json_get_proxy Json_proxy_base<T>::get(Str key, unsigned long long& x) const {
    return cvalue().get(key, x);
}
template <typename T>
inline const Json_get_proxy Json_proxy_base<T>::get(Str key, double& x) const {
    return cvalue().get(key, x);
}
template <typename T>
inline const Json_get_proxy Json_proxy_base<T>::get(Str key, bool& x) const {
    return cvalue().get(key, x);
}
template <typename T>
inline const Json_get_proxy Json_proxy_base<T>::get(Str key, Str& x) const {
    return cvalue().get(key, x);
}
template <typename T>
inline const Json_get_proxy Json_proxy_base<T>::get(Str key, String& x) const {
    return cvalue().get(key, x);
}


/** @brief Construct a null Json. */
inline Json::Json() {
    memset(&u_, 0, sizeof(u_));
}
/** @brief Construct a null Json. */
inline Json::Json(const null_t&) {
    memset(&u_, 0, sizeof(u_));
}
/** @brief Construct a Json copy of @a x. */
inline Json::Json(const Json& x)
    : u_(x.u_) {
    if (u_.x.type < 0)
        u_.str.ref();
    if (u_.x.x && (u_.x.type == j_array || u_.x.type == j_object))
        u_.x.x->ref();
}
/** @overload */
template <typename P> inline Json::Json(const Json_proxy_base<P>& x)
    : u_(x.cvalue().u_) {
    if (u_.x.type < 0)
        u_.str.ref();
    if (u_.x.x && (u_.x.type == j_array || u_.x.type == j_object))
        u_.x.x->ref();
}
#if HAVE_CXX_RVALUE_REFERENCES
/** @overload */
inline Json::Json(Json&& x)
    : u_(std::move(x.u_)) {
    memset(&x, 0, sizeof(x));
}
#endif
/** @brief Construct simple Json values. */
inline Json::Json(int x) {
    u_.i.x = x;
    u_.i.type = j_int;
}
inline Json::Json(unsigned x) {
    u_.u.x = x;
    u_.u.type = j_unsigned;
}
inline Json::Json(long x) {
    u_.i.x = x;
    u_.i.type = j_int;
}
inline Json::Json(unsigned long x) {
    u_.u.x = x;
    u_.u.type = j_unsigned;
}
inline Json::Json(long long x) {
    u_.i.x = x;
    u_.i.type = j_int;
}
inline Json::Json(unsigned long long x) {
    u_.u.x = x;
    u_.u.type = j_unsigned;
}
inline Json::Json(double x) {
    u_.d.x = x;
    u_.d.type = j_double;
}
inline Json::Json(bool x) {
    u_.i.x = x;
    u_.i.type = j_bool;
}
inline Json::Json(const String& x) {
    u_.str = x.internal_rep();
    u_.str.ref();
}
inline Json::Json(const std::string& x) {
    u_.str.reset_ref();
    String str(x);
    str.swap(u_.str);
}
inline Json::Json(Str x) {
    u_.str.reset_ref();
    String str(x);
    str.swap(u_.str);
}
inline Json::Json(const char* x) {
    u_.str.reset_ref();
    String str(x);
    str.swap(u_.str);
}
/** @brief Construct an array Json containing the elements of @a x. */
template <typename T>
inline Json::Json(const std::vector<T> &x) {
    u_.a.type = j_array;
    u_.a.x = ArrayJson::make(int(x.size()));
    for (typename std::vector<T>::const_iterator it = x.begin();
         it != x.end(); ++it) {
        new((void*) &u_.a.x->a[u_.a.x->size]) Json(*it);
        ++u_.a.x->size;
    }
}

template <typename T> struct Json_iterator_initializer {
    template <typename I>
    static inline void run(Json& j, I first, I last) {
        for (j = Json::make_array(); first != last; ++first)
            j.push_back(Json(*first));
    }
};
template <typename T, typename U>
struct Json_iterator_initializer<std::pair<T, U> > {
    template <typename I>
    static inline void run(Json& j, I first, I last) {
        for (j = Json::make_object(); first != last; ++first)
            j.set((*first).first, (*first).second);
    }
};

/** @brief Construct an array Json containing the elements in [@a first,
    @a last). */
template <typename T>
inline Json::Json(T first, T last) {
    u_.a.type = 0;
    Json_iterator_initializer<typename std::iterator_traits<T>::value_type>::run(*this, first, last);
}

/** @cond never */
inline void Json::deref() {
    if (u_.x.type < 0)
        u_.str.deref();
    else if (u_.x.x && unsigned(u_.x.type - j_array) < 2)
        u_.x.x->deref(json_type(u_.x.type));
}
/** @endcond never */

inline Json::~Json() {
    deref();
}

/** @brief Return an empty array-valued Json. */
inline Json Json::make_array() {
    Json j;
    j.u_.x.type = j_array;
    return j;
}
/** @brief Return an array-valued Json containing @a args. */
template <typename... Args>
inline Json Json::array(Args&&... args) {
    Json j;
    j.u_.x.type = j_array;
    j.reserve(sizeof...(args));
    j.push_back_list(std::forward<Args>(args)...);
    return j;
}
/** @brief Return an empty array-valued Json with reserved space for @a n items. */
inline Json Json::make_array_reserve(int n) {
    Json j;
    j.u_.a.type = j_array;
    j.u_.a.x = n ? ArrayJson::make(n) : 0;
    return j;
}
/** @brief Return an empty object-valued Json. */
inline Json Json::make_object() {
    Json j;
    j.u_.o.type = j_object;
    return j;
}
/** @brief Return an empty object-valued Json. */
template <typename... Args>
inline Json Json::object(Args&&... rest) {
    Json j;
    j.u_.o.type = j_object;
    j.set_list(std::forward<Args>(rest)...);
    return j;
}
/** @brief Return a string-valued Json. */
inline Json Json::make_string(const String& x) {
    return Json(x);
}
/** @overload */
inline Json Json::make_string(const std::string& x) {
    return Json(x);
}
/** @overload */
inline Json Json::make_string(const char *s, int len) {
    return Json(String(s, len));
}

/** @brief Test if this Json is truthy. */
inline bool Json::truthy() const {
    return (u_.x.x ? u_.x.type >= 0 || u_.str.length
            : (unsigned) (u_.x.type - 1) < (unsigned) (j_int - 1));
}
/** @brief Test if this Json is falsy. */
inline bool Json::falsy() const {
    return !truthy();
}
/** @brief Test if this Json is truthy.
    @sa empty() */
inline Json::operator unspecified_bool_type() const {
    return truthy() ? &Json::is_null : 0;
}
/** @brief Return true if this Json is falsy. */
inline bool Json::operator!() const {
    return !truthy();
}

/** @brief Test this Json's type. */
inline bool Json::is_null() const {
    return !u_.x.x && u_.x.type == j_null;
}
inline bool Json::is_int() const {
    return unsigned(u_.x.type - j_int) <= unsigned(j_unsigned - j_int);
}
inline bool Json::is_i() const {
    return is_int();
}
inline bool Json::is_unsigned() const {
    return u_.x.type == j_unsigned;
}
inline bool Json::is_u() const {
    return is_unsigned();
}
inline bool Json::is_signed() const {
    return u_.x.type == j_int;
}
inline bool Json::is_nonnegint() const {
    return u_.x.type == j_unsigned
        || (u_.x.type == j_int && u_.i.x >= 0);
}
inline bool Json::is_double() const {
    return u_.x.type == j_double;
}
inline bool Json::is_d() const {
    return is_double();
}
inline bool Json::is_number() const {
    return unsigned(u_.x.type - j_int) <= unsigned(j_double - j_int);
}
inline bool Json::is_n() const {
    return is_number();
}
inline bool Json::is_bool() const {
    return u_.x.type == j_bool;
}
inline bool Json::is_b() const {
    return is_bool();
}
inline bool Json::is_string() const {
    return u_.x.x && u_.x.type <= 0;
}
inline bool Json::is_s() const {
    return is_string();
}
inline bool Json::is_array() const {
    return u_.x.type == j_array;
}
inline bool Json::is_a() const {
    return is_array();
}
inline bool Json::is_object() const {
    return u_.x.type == j_object;
}
inline bool Json::is_o() const {
    return is_object();
}
/** @brief Test if this Json is a primitive value, not including null. */
inline bool Json::is_primitive() const {
    return u_.x.type >= j_int || (u_.x.x && u_.x.type <= 0);
}

/** @brief Return true if this Json is null, an empty array, or an empty
    object. */
inline bool Json::empty() const {
    return unsigned(u_.x.type) < unsigned(j_int)
        && (!u_.x.x || u_.x.x->size == 0);
}
/** @brief Return the number of elements in this complex Json.
    @pre is_array() || is_object() || is_null() */
inline Json::size_type Json::size() const {
    precondition(unsigned(u_.x.type) < unsigned(j_int));
    return u_.x.x ? u_.x.x->size : 0;
}
/** @brief Test if this complex Json is shared. */
inline bool Json::shared() const {
    return u_.x.x && (u_.x.type == j_array || u_.x.type == j_object)
        && u_.x.x->refcount != 1;
}

// Primitive methods

/** @brief Return this Json converted to an integer.

    Converts any Json to an integer value. Numeric Jsons convert as you'd
    expect. Null Jsons convert to 0; false boolean Jsons to 0 and true
    boolean Jsons to 1; string Jsons to a number parsed from their initial
    portions; and array and object Jsons to size().
    @sa as_i() */
inline int64_t Json::to_i() const {
    if (is_int())
        return u_.i.x;
    else
        return hard_to_i();
}

inline uint64_t Json::to_u() const {
    if (is_int())
        return u_.u.x;
    else
        return hard_to_u();
}

/** @brief Extract this integer Json's value into @a x.
    @param[out] x value storage
    @return True iff this Json stores an integer value.

    If false is returned (!is_number() or the number is not parseable as a
    pure integer), @a x remains unchanged. */
inline bool Json::to_i(int& x) const {
    if (is_int()) {
        x = u_.i.x;
        return true;
    } else if (is_double() && u_.d.x == double(int(u_.d.x))) {
        x = int(u_.d.x);
        return true;
    } else
        return false;
}

/** @overload */
inline bool Json::to_i(unsigned& x) const {
    if (is_int()) {
        x = u_.u.x;
        return true;
    } else if (is_double() && u_.d.x == double(unsigned(u_.d.x))) {
        x = unsigned(u_.d.x);
        return true;
    } else
        return false;
}

/** @overload */
inline bool Json::to_i(long& x) const {
    if (is_int()) {
        x = u_.i.x;
        return true;
    } else if (is_double() && u_.d.x == double(long(u_.d.x))) {
        x = long(u_.d.x);
        return true;
    } else
        return false;
}

/** @overload */
inline bool Json::to_i(unsigned long& x) const {
    if (is_int()) {
        x = u_.u.x;
        return true;
    } else if (is_double() && u_.d.x == double((unsigned long) u_.d.x)) {
        x = (unsigned long) u_.d.x;
        return true;
    } else
        return false;
}

/** @overload */
inline bool Json::to_i(long long& x) const {
    if (is_int()) {
        x = u_.i.x;
        return true;
    } else if (is_double() && u_.d.x == double((long long) u_.d.x)) {
        x = (long long) u_.d.x;
        return true;
    } else
        return false;
}

/** @overload */
inline bool Json::to_i(unsigned long long& x) const {
    if (is_int()) {
        x = u_.u.x;
        return true;
    } else if (is_double() && u_.d.x == double((unsigned long long) u_.d.x)) {
        x = (unsigned long long) u_.d.x;
        return true;
    } else
        return false;
}

/** @brief Return this Json converted to a 64-bit unsigned integer.

    See to_i() for the conversion rules. */
inline uint64_t Json::to_u64() const {
    return to_u();
}

/** @brief Return the integer value of this numeric Json.
    @pre is_number()
    @sa to_i() */
inline int64_t Json::as_i() const {
    precondition(is_int() || is_double());
    return is_int() ? u_.i.x : int64_t(u_.d.x);
}

/** @brief Return the integer value of this numeric Json or @a default_value. */
inline int64_t Json::as_i(int64_t default_value) const {
    if (is_int() || is_double())
        return as_i();
    else
        return default_value;
}

/** @brief Return the unsigned integer value of this numeric Json.
    @pre is_number()
    @sa to_i() */
inline uint64_t Json::as_u() const {
    precondition(is_int() || is_double());
    return is_int() ? u_.u.x : uint64_t(u_.d.x);
}

/** @brief Return the integer value of this numeric Json or @a default_value. */
inline uint64_t Json::as_u(uint64_t default_value) const {
    if (is_int() || is_double())
        return as_u();
    else
        return default_value;
}

/** @brief Return this Json converted to a double.

    Converts any Json to an integer value. Numeric Jsons convert as you'd
    expect. Null Jsons convert to 0; false boolean Jsons to 0 and true
    boolean Jsons to 1; string Jsons to a number parsed from their initial
    portions; and array and object Jsons to size().
    @sa as_d() */
inline double Json::to_d() const {
    if (is_double())
        return u_.d.x;
    else
        return hard_to_d();
}

/** @brief Extract this numeric Json's value into @a x.
    @param[out] x value storage
    @return True iff is_number().

    If !is_number(), @a x remains unchanged. */
inline bool Json::to_d(double& x) const {
    if (is_double() || is_int()) {
        x = to_d();
        return true;
    } else
        return false;
}

/** @brief Return the double value of this numeric Json.
    @pre is_number()
    @sa to_d() */
inline double Json::as_d() const {
    precondition(is_double() || is_int());
    if (is_double())
        return u_.d.x;
    else if (u_.x.type == j_int)
        return u_.i.x;
    else
        return u_.u.x;
}

/** @brief Return the double value of this numeric Json or @a default_value. */
inline double Json::as_d(double default_value) const {
    if (!is_double() && !is_int())
        return default_value;
    else
        return as_d();
}

/** @brief Return this Json converted to a boolean.

    Converts any Json to a boolean value. Boolean Jsons convert as you'd
    expect. Null Jsons convert to false; zero-valued numeric Jsons to false,
    and other numeric Jsons to true; empty string Jsons to false, and other
    string Jsons to true; and array and object Jsons to !empty().
    @sa as_b() */
inline bool Json::to_b() const {
    if (is_bool())
        return u_.i.x;
    else
        return hard_to_b();
}

/** @brief Extract this boolean Json's value into @a x.
    @param[out] x value storage
    @return True iff is_bool().

    If !is_bool(), @a x remains unchanged. */
inline bool Json::to_b(bool& x) const {
    if (is_bool()) {
        x = u_.i.x;
        return true;
    } else
        return false;
}

/** @brief Return the value of this boolean Json.
    @pre is_bool()
    @sa to_b() */
inline bool Json::as_b() const {
    precondition(is_bool());
    return u_.i.x;
}

/** @brief Return the boolean value of this numeric Json or @a default_value. */
inline bool Json::as_b(bool default_value) const {
    if (is_bool())
        return as_b();
    else
        return default_value;
}

/** @brief Return this Json converted to a string.

    Converts any Json to a string value. String Jsons convert as you'd expect.
    Null Jsons convert to the empty string; numeric Jsons to their string
    values; boolean Jsons to "false" or "true"; and array and object Jsons to
    "[Array]" and "[Object]", respectively.
    @sa as_s() */
inline String Json::to_s() const {
    if (u_.x.type <= 0 && u_.x.x)
        return String(u_.str);
    else
        return hard_to_s();
}

/** @brief Extract this string Json's value into @a x.
    @param[out] x value storage
    @return True iff is_string().

    If !is_string(), @a x remains unchanged. */
inline bool Json::to_s(Str& x) const {
    if (u_.x.type <= 0 && u_.x.x) {
        x.assign(u_.str.data, u_.str.length);
        return true;
    } else
        return false;
}

/** @brief Extract this string Json's value into @a x.
    @param[out] x value storage
    @return True iff is_string().

    If !is_string(), @a x remains unchanged. */
inline bool Json::to_s(String& x) const {
    if (u_.x.type <= 0 && u_.x.x) {
        x.assign(u_.str);
        return true;
    } else
        return false;
}

/** @brief Return the value of this string Json.
    @pre is_string()
    @sa to_s() */
inline const String& Json::as_s() const {
    precondition(u_.x.type <= 0 && u_.x.x);
    return reinterpret_cast<const String&>(u_.str);
}

/** @overload */
inline String& Json::as_s() {
    precondition(u_.x.type <= 0 && u_.x.x);
    return reinterpret_cast<String&>(u_.str);
}

/** @brief Return the value of this string Json or @a default_value. */
inline const String& Json::as_s(const String& default_value) const {
    if (u_.x.type > 0 || !u_.x.x)
        return default_value;
    else
        return as_s();
}

inline void Json::force_number() {
    precondition((u_.x.type == j_null && !u_.x.x) || u_.x.type == j_int || u_.x.type == j_double);
    if (u_.x.type == j_null && !u_.x.x)
        u_.x.type = j_int;
}

inline void Json::force_double() {
    precondition((u_.x.type == j_null && !u_.x.x) || u_.x.type == j_int || u_.x.type == j_double);
    if (u_.x.type == j_null && !u_.x.x)
        u_.x.type = j_double;
    else if (u_.x.type == j_int) {
        u_.d.x = u_.i.x;
        u_.d.type = j_double;
    } else if (u_.x.type == j_unsigned) {
        u_.d.x = u_.u.x;
        u_.d.type = j_double;
    }
}


// Object methods

/** @brief Return 1 if this object Json contains @a key, 0 otherwise.

    Returns 0 if this is not an object Json. */
inline Json::size_type Json::count(Str key) const {
    precondition(u_.x.type == j_null || u_.x.type == j_object);
    return u_.o.x ? ojson()->find(key.data(), key.length()) >= 0 : 0;
}

/** @brief Return the value at @a key in an object or array Json.

    If this is an array Json, and @a key is the simplest base-10
    representation of an integer <em>i</em>, then returns get(<em>i</em>). If
    this is neither an array nor an object, returns a null Json. */
inline const Json& Json::get(Str key) const {
    int i;
    ObjectJson *oj;
    if (is_object() && (oj = ojson())
        && (i = oj->find(key.data(), key.length())) >= 0)
        return oj->item(i).v_.second;
    else
        return hard_get(key);
}

/** @brief Return a reference to the value of @a key in an object Json.

    This Json is first converted to an object. Arrays are converted to objects
    with numeric keys. Other types of Json are converted to empty objects.
    If !count(@a key), then a null Json is inserted at @a key. */
inline Json& Json::get_insert(const String &key) {
    uniqueify_object(true);
    return ojson()->get_insert(key);
}

/** @overload */
inline Json& Json::get_insert(Str key) {
    uniqueify_object(true);
    return ojson()->get_insert(key);
}

/** @overload */
inline Json& Json::get_insert(const char *key) {
    uniqueify_object(true);
    return ojson()->get_insert(Str(key));
}

/** @brief Return get(@a key).to_i(). */
inline long Json::get_i(Str key) const {
    return get(key).to_i();
}

/** @brief Return get(@a key).to_d(). */
inline double Json::get_d(Str key) const {
    return get(key).to_d();
}

/** @brief Return get(@a key).to_b(). */
inline bool Json::get_b(Str key) const {
    return get(key).to_b();
}

/** @brief Return get(@a key).to_s(). */
inline String Json::get_s(Str key) const {
    return get(key).to_s();
}

/** @brief Extract this object Json's value at @a key into @a x.
    @param[out] x value storage
    @return proxy for *this

    @a x is assigned iff count(@a key). The return value is a proxy
    object that mostly behaves like *this. However, the proxy is "truthy"
    iff count(@a key) and @a x was assigned. The proxy also has status()
    methods that return the extraction status. For example:

    <code>
    Json j = Json::parse("{\"a\":1,\"b\":2}"), x;
    assert(j.get("a", x));            // extraction succeeded
    assert(x == Json(1));
    assert(!j.get("c", x));           // no "c" key
    assert(x == Json(1));             // x remains unchanged
    assert(!j.get("c", x).status());  // can use ".status()" for clarity

    // Can chain .get() methods to extract multiple values
    Json a, b, c;
    j.get("a", a).get("b", b);
    assert(a == Json(1) && b == Json(2));

    // Use .status() to return or assign extraction status
    bool a_status, b_status, c_status;
    j.get("a", a).status(a_status)
     .get("b", b).status(b_status)
     .get("c", c).status(c_status);
    assert(a_status && b_status && !c_status);
    </code>

    Overloaded versions of @a get() can extract integer, double, boolean,
    and string values for specific keys. These versions succeed iff
    count(@a key) and the corresponding value has the expected type. For
    example:

    <code>
    Json j = Json::parse("{\"a\":1,\"b\":\"\"}");
    int a, b;
    bool a_status, b_status;
    j.get("a", a).status(a_status).get("b", b).status(b_status);
    assert(a_status && a == 1 && !b_status);
    </code> */
inline const Json_get_proxy Json::get(Str key, Json& x) const {
    int i;
    ObjectJson *oj;
    if (is_object() && (oj = ojson())
        && (i = oj->find(key.data(), key.length())) >= 0) {
        x = oj->item(i).v_.second;
        return Json_get_proxy(*this, true);
    } else
        return Json_get_proxy(*this, false);
}

/** @overload */
inline const Json_get_proxy Json::get(Str key, int &x) const {
    return Json_get_proxy(*this, get(key).to_i(x));
}

/** @overload */
inline const Json_get_proxy Json::get(Str key, unsigned& x) const {
    return Json_get_proxy(*this, get(key).to_i(x));
}

/** @overload */
inline const Json_get_proxy Json::get(Str key, long& x) const {
    return Json_get_proxy(*this, get(key).to_i(x));
}

/** @overload */
inline const Json_get_proxy Json::get(Str key, unsigned long& x) const {
    return Json_get_proxy(*this, get(key).to_i(x));
}

/** @overload */
inline const Json_get_proxy Json::get(Str key, long long& x) const {
    return Json_get_proxy(*this, get(key).to_i(x));
}

/** @overload */
inline const Json_get_proxy Json::get(Str key, unsigned long long& x) const {
    return Json_get_proxy(*this, get(key).to_i(x));
}

/** @overload */
inline const Json_get_proxy Json::get(Str key, double& x) const {
    return Json_get_proxy(*this, get(key).to_d(x));
}

/** @overload */
inline const Json_get_proxy Json::get(Str key, bool& x) const {
    return Json_get_proxy(*this, get(key).to_b(x));
}

/** @overload */
inline const Json_get_proxy Json::get(Str key, Str& x) const {
    return Json_get_proxy(*this, get(key).to_s(x));
}

/** @overload */
inline const Json_get_proxy Json::get(Str key, String& x) const {
    return Json_get_proxy(*this, get(key).to_s(x));
}


/** @brief Return the value at @a key in an object or array Json.
    @sa Json::get() */
inline const Json& Json::operator[](Str key) const {
    return get(key);
}

/** @brief Return a proxy reference to the value at @a key in an object Json.

    Returns the current @a key value if it exists. Otherwise, returns a proxy
    that acts like a null Json. If this proxy is assigned, this Json is
    converted to an object as by get_insert(), and then extended as necessary
    to contain the new value. */
inline Json_object_proxy<Json> Json::operator[](const String& key) {
    return Json_object_proxy<Json>(*this, key);
}

/** @overload */
inline Json_object_str_proxy<Json> Json::operator[](const std::string& key) {
    return Json_object_str_proxy<Json>(*this, Str(key.data(), key.length()));
}

/** @overload */
inline Json_object_str_proxy<Json> Json::operator[](Str key) {
    return Json_object_str_proxy<Json>(*this, key);
}

/** @overload */
inline Json_object_str_proxy<Json> Json::operator[](const char* key) {
    return Json_object_str_proxy<Json>(*this, key);
}

/** @brief Return the value at @a key in an object Json.
    @pre is_object() && count(@a key) */
inline const Json& Json::at(Str key) const {
    precondition(is_object() && u_.o.x);
    ObjectJson *oj = ojson();
    int i = oj->find(key.data(), key.length());
    precondition(i >= 0);
    return oj->item(i).v_.second;
}

/** @brief Return a reference to the value at @a key in an object Json.
    @pre is_object()

    Returns a newly-inserted null Json if !count(@a key). */
inline Json& Json::at_insert(const String &key) {
    precondition(is_object());
    return get_insert(key);
}

/** @overload */
inline Json& Json::at_insert(Str key) {
    precondition(is_object());
    return get_insert(key);
}

/** @overload */
inline Json& Json::at_insert(const char *key) {
    precondition(is_object());
    return get_insert(Str(key));
}

/** @brief Set the value of @a key to @a value in this object Json.
    @return this Json

    An array Json is converted to an object Json with numeric keys. Other
    non-object Jsons are converted to empty objects. */
inline Json& Json::set(const String& key, Json value) {
    uniqueify_object(true);
    ojson()->get_insert(key) = std::move(value);
    return *this;
}

/** @overload */
template <typename P>
inline Json& Json::set(const String& key, const Json_proxy_base<P>& value) {
    uniqueify_object(true);
    ojson()->get_insert(key) = value.cvalue();
    return *this;
}

/** @brief Remove the value of @a key from an object Json.
    @return this Json
    @sa erase() */
inline Json& Json::unset(Str key) {
    if (is_object()) {
        uniqueify_object(true);
        ojson()->erase(key);
    }
    return *this;
}

/** @brief Add the key-value pairs [key, value, ...] to the object.
    @return this Json

    An array Json is converted to an object Json with numeric keys. Other
    non-object Jsons are converted to empty objects. */
template <typename T, typename... Args>
inline Json& Json::set_list(const String& key, T value, Args&&... rest) {
    set(key, std::move(value));
    set_list(std::forward<Args>(rest)...);
    return *this;
}

/** @overload */
inline Json& Json::set_list() {
    return *this;
}

/** @brief Insert element @a x in this object Json.
    @param x Pair of key and value.
    @return Pair of iterator pointing to key's value and bool indicating
    whether the value is newly inserted.
    @pre is_object()

    An existing element with key @a x.first is not replaced. */
inline std::pair<Json::object_iterator, bool> Json::insert(const object_value_type& x) {
    precondition(is_object());
    uniqueify_object(false);
    ObjectJson *oj = ojson();
    int n = oj->n_, i = oj->find_insert(x.first, x.second);
    return std::make_pair(object_iterator(this, i), i == n);
}

/** @brief Insert element @a x in this object Json.
    @param position Ignored.
    @param x Pair of key and value.
    @return Pair of iterator pointing to key's value and bool indicating
    whether the value is newly inserted.
    @pre is_object()

    An existing element with key @a x.first is not replaced. */
inline Json::object_iterator Json::insert(object_iterator position, const object_value_type& x) {
    (void) position;
    return insert(x).first;
}

/** @brief Remove the value pointed to by @a it from an object Json.
    @pre is_object()
    @return Next iterator */
inline Json::object_iterator Json::erase(Json::object_iterator it) {
    precondition(is_object() && it.live() && it.j_ == this);
    uniqueify_object(false);
    ojson()->erase(it.i_);
    ++it;
    return it;
}

/** @brief Remove the value of @a key from an object Json.
    @pre is_object()
    @return Number of items removed */
inline Json::size_type Json::erase(Str key) {
    precondition(is_object());
    uniqueify_object(false);
    return ojson()->erase(key);
}

/** @brief Merge the values of object Json @a x into this object Json.
    @pre (is_object() || is_null()) && (x.is_object() || x.is_null())
    @return this Json

    The key-value pairs in @a x are assigned to this Json. Null Jsons are
    silently converted to empty objects, except that if @a x and this Json are
    both null, then this Json is left as null. */
inline Json& Json::merge(const Json& x) {
    precondition(is_object() || is_null());
    precondition(x.is_object() || x.is_null());
    if (x.u_.o.x) {
        uniqueify_object(false);
        ObjectJson *oj = ojson(), *xoj = x.ojson();
        const ObjectItem *xb = xoj->os_, *xe = xb + xoj->n_;
        for (; xb != xe; ++xb)
            if (xb->next_ > -2)
                oj->get_insert(xb->v_.first) = xb->v_.second;
    }
    return *this;
}

/** @cond never */
template <typename U>
inline Json& Json::merge(const Json_proxy_base<U>& x) {
    return merge(x.cvalue());
}
/** @endcond never */


// ARRAY METHODS

/** @brief Return the @a x th array element.

    If @a x is out of range of this array, returns a null Json. If this is an
    object Json, then returns get(String(@a x)). If this is neither an object
    nor an array, returns a null Json. */
inline const Json& Json::get(size_type x) const {
    ArrayJson *aj;
    if (u_.x.type == j_array && (aj = ajson()) && unsigned(x) < unsigned(aj->size))
        return aj->a[x];
    else
        return hard_get(x);
}

/** @brief Return a reference to the @a x th array element.

    If this Json is an object, returns get_insert(String(x)). Otherwise this
    Json is first converted to an array; non-arrays are converted to empty
    arrays. The array is extended if @a x is out of range. */
inline Json& Json::get_insert(size_type x) {
    ArrayJson *aj;
    if (u_.x.type == j_array && (aj = ajson()) && aj->refcount == 1
        && unsigned(x) < unsigned(aj->size))
        return aj->a[x];
    else
        return hard_get_insert(x);
}

/** @brief Return the @a x th element in an array Json.
    @pre is_array()

    A null Json is treated like an empty array. */
inline const Json& Json::at(size_type x) const {
    precondition(is_array());
    return get(x);
}

/** @brief Return a reference to the @a x th element in an array Json.
    @pre is_array()

    The array is extended if @a x is out of range. */
inline Json& Json::at_insert(size_type x) {
    precondition(is_array());
    return get_insert(x);
}

/** @brief Return the @a x th array element.

    If @a x is out of range of this array, returns a null Json. If this is an
    object Json, then returns get(String(@a x)). If this is neither an object
    nor an array, returns a null Json. */
inline const Json& Json::operator[](size_type x) const {
    return get(x);
}

/** @brief Return a proxy reference to the @a x th array element.

    If this Json is an object, returns operator[](String(x)). If this Json is
    an array and @a x is in range, returns that element. Otherwise, returns a
    proxy that acts like a null Json. If this proxy is assigned, this Json is
    converted to an array, and then extended as necessary to contain the new
    value. */
inline Json_array_proxy<Json> Json::operator[](size_type x) {
    return Json_array_proxy<Json>(*this, x);
}

/** @brief Return the last array element.
    @pre is_array() && !empty() */
inline const Json& Json::back() const {
    precondition(is_array() && u_.a.x && u_.a.x->size > 0);
    return u_.a.x->a[u_.a.x->size - 1];
}

/** @brief Return a reference to the last array element.
    @pre is_array() && !empty() */
inline Json& Json::back() {
    precondition(is_array() && u_.a.x && u_.a.x->size > 0);
    uniqueify_array(false, 0);
    return u_.a.x->a[u_.a.x->size - 1];
}

/** @brief Push an element onto the back of the array.
    @pre is_array() || is_null()
    @return this Json

    A null Json is promoted to an array. */
inline Json& Json::push_back(Json x) {
    new(uniqueify_array_insert(false, -1)) Json(std::move(x));
    return *this;
}

/** @overload */
template <typename P> inline Json& Json::push_back(const Json_proxy_base<P>& x) {
    new(uniqueify_array_insert(false, -1)) Json(x.cvalue());
    return *this;
}

/** @brief Remove the last element from an array.
    @pre is_array() && !empty() */
inline void Json::pop_back() {
    precondition(is_array() && u_.a.x && u_.a.x->size > 0);
    uniqueify_array(false, 0);
    --u_.a.x->size;
    u_.a.x->a[u_.a.x->size].~Json();
}

inline Json& Json::push_back_list() {
    return *this;
}

/** @brief Insert the items [first, rest...] onto the back of this array.
    @pre is_array() || is_null()
    @return this Json

    A null Json is promoted to an array. */
template <typename T, typename... Args>
inline Json& Json::push_back_list(T first, Args&&... rest) {
    push_back(std::move(first));
    push_back_list(std::forward<Args>(rest)...);
    return *this;
}


/** @brief Insert an element into the array.
    @pre is_array() || is_null()
    @return this Json

    A null Json is promoted to an array. */
inline Json::array_iterator Json::insert(array_iterator position, Json x) {
    precondition(position >= abegin() && position <= aend());
    size_type pos = position - abegin();
    new(uniqueify_array_insert(false, pos)) Json(std::move(x));
    return abegin() + pos;
}

/** @overload */
template <typename P> inline Json::array_iterator Json::insert(array_iterator position, const Json_proxy_base<P>& x) {
    precondition(position >= abegin() && position <= aend());
    size_type pos = position - abegin();
    new(uniqueify_array_insert(false, pos)) Json(x.cvalue());
    return abegin() + pos;
}

inline Json::array_iterator Json::erase(array_iterator position) {
    return erase(position, position + 1);
}


inline Json* Json::array_data() {
    precondition(is_null() || is_array());
    uniqueify_array(false, 0);
    return u_.a.x ? u_.a.x->a : 0;
}

inline const Json* Json::array_data() const {
    precondition(is_null() || is_array());
    return u_.a.x ? u_.a.x->a : 0;
}

inline const Json* Json::array_cdata() const {
    precondition(is_null() || is_array());
    return u_.a.x ? u_.a.x->a : 0;
}

inline Json* Json::end_array_data() {
    precondition(is_null() || is_array());
    uniqueify_array(false, 0);
    return u_.a.x ? u_.a.x->a + u_.a.x->size : 0;
}

inline const Json* Json::end_array_data() const {
    precondition(is_null() || is_array());
    return u_.a.x ? u_.a.x->a + u_.a.x->size : 0;
}

inline const Json* Json::end_array_cdata() const {
    precondition(is_null() || is_array());
    return u_.a.x ? u_.a.x->a + u_.a.x->size : 0;
}


inline Json::const_object_iterator Json::cobegin() const {
    precondition(is_null() || is_object());
    return const_object_iterator(this, 0);
}

inline Json::const_object_iterator Json::coend() const {
    precondition(is_null() || is_object());
    return const_object_iterator(this, -1);
}

inline Json::const_object_iterator Json::obegin() const {
    return cobegin();
}

inline Json::const_object_iterator Json::oend() const {
    return coend();
}

inline Json::object_iterator Json::obegin() {
    precondition(is_null() || is_object());
    return object_iterator(this, 0);
}

inline Json::object_iterator Json::oend() {
    precondition(is_null() || is_object());
    return object_iterator(this, -1);
}

inline Json::const_array_iterator Json::cabegin() const {
    precondition(is_null() || is_array());
    return const_array_iterator(this, 0);
}

inline Json::const_array_iterator Json::caend() const {
    precondition(is_null() || is_array());
    ArrayJson *aj = ajson();
    return const_array_iterator(this, aj ? aj->size : 0);
}

inline Json::const_array_iterator Json::abegin() const {
    return cabegin();
}

inline Json::const_array_iterator Json::aend() const {
    return caend();
}

inline Json::array_iterator Json::abegin() {
    precondition(is_null() || is_array());
    return array_iterator(this, 0);
}

inline Json::array_iterator Json::aend() {
    precondition(is_null() || is_array());
    ArrayJson *aj = ajson();
    return array_iterator(this, aj ? aj->size : 0);
}

inline Json::const_iterator Json::cbegin() const {
    return const_iterator(this, 0);
}

inline Json::const_iterator Json::cend() const {
    return const_iterator(this, -1);
}

inline Json::iterator Json::begin() {
    return iterator(this, 0);
}

inline Json::iterator Json::end() {
    return iterator(this, -1);
}

inline Json::const_iterator Json::begin() const {
    return cbegin();
}

inline Json::const_iterator Json::end() const {
    return cend();
}


// Unparsing
class Json::unparse_manipulator {
  public:
    unparse_manipulator()
        : indent_depth_(0), tab_width_(0), newline_terminator_(false),
          space_separator_(false) {
    }
    int indent_depth() const {
        return indent_depth_;
    }
    unparse_manipulator indent_depth(int x) const {
        unparse_manipulator m(*this);
        m.indent_depth_ = x;
        return m;
    }
    int tab_width() const {
        return tab_width_;
    }
    unparse_manipulator tab_width(int x) const {
        unparse_manipulator m(*this);
        m.tab_width_ = x;
        return m;
    }
    bool newline_terminator() const {
        return newline_terminator_;
    }
    unparse_manipulator newline_terminator(bool x) const {
        unparse_manipulator m(*this);
        m.newline_terminator_ = x;
        return m;
    }
    bool space_separator() const {
        return space_separator_;
    }
    unparse_manipulator space_separator(bool x) const {
        unparse_manipulator m(*this);
        m.space_separator_ = x;
        return m;
    }
    bool empty() const {
        return !indent_depth_ && !newline_terminator_ && !space_separator_;
    }
  private:
    int indent_depth_;
    int tab_width_;
    bool newline_terminator_;
    bool space_separator_;
};

inline Json::unparse_manipulator Json::indent_depth(int x) {
    return unparse_manipulator().indent_depth(x);
}
inline Json::unparse_manipulator Json::tab_width(int x) {
    return unparse_manipulator().tab_width(x);
}
inline Json::unparse_manipulator Json::newline_terminator(bool x) {
    return unparse_manipulator().newline_terminator(x);
}
inline Json::unparse_manipulator Json::space_separator(bool x) {
    return unparse_manipulator().space_separator(x);
}

/** @brief Return the string representation of this Json. */
inline String Json::unparse() const {
    StringAccum sa;
    hard_unparse(sa, default_manipulator, 0);
    return sa.take_string();
}

/** @brief Return the string representation of this Json.
    @param add_newline If true, add a final newline. */
inline String Json::unparse(const unparse_manipulator &m) const {
    StringAccum sa;
    hard_unparse(sa, m, 0);
    return sa.take_string();
}

/** @brief Unparse the string representation of this Json into @a sa. */
inline void Json::unparse(StringAccum &sa) const {
    hard_unparse(sa, default_manipulator, 0);
}

/** @brief Unparse the string representation of this Json into @a sa. */
inline void Json::unparse(StringAccum &sa, const unparse_manipulator &m) const {
    hard_unparse(sa, m, 0);
}


// Parsing

class Json::streaming_parser {
  public:
    inline streaming_parser();
    inline void reset();

    inline bool done() const;
    inline bool success() const;
    inline bool error() const;

    inline size_t consume(const char* first, size_t length,
                          const String& str = String(),
                          bool complete = false);
    inline const char* consume(const char* first, const char* last,
                               const String& str = String(),
                               bool complete = false);
    const uint8_t* consume(const uint8_t* first, const uint8_t* last,
                           const String& str = String(),
                           bool complete = false);

    inline Json& result();
    inline const Json& result() const;

  private:
    enum {
        st_final = -2, st_error = -1,
        st_partlenmask = 0x0F, st_partmask = 0xFF,
        st_stringpart = 0x10, st_primitivepart = 0x20, st_numberpart = 0x40,

        st_initial = 0x000,
        st_array_initial = 0x100,
        st_array_value = 0x200,
        st_object_value = 0x300,

        st_array_delim = 0x400,
        st_object_delim = 0x500,
        st_object_initial = 0x600,
        st_object_key = 0x700,
        st_object_colon = 0x800,

        max_depth = 2048
    };

    int state_;
    std::vector<Json*> stack_;
    String str_;
    Json json_;

    inline Json* current();
    const uint8_t* error_at(const uint8_t* here);
    const uint8_t* consume_string(const uint8_t* first, const uint8_t* last, const String& str);
    const uint8_t* consume_backslash(StringAccum& sa, const uint8_t* first, const uint8_t* last);
    const uint8_t* consume_stringpart(StringAccum& sa, const uint8_t* first, const uint8_t* last);
    const uint8_t* consume_primitive(const uint8_t* first, const uint8_t* last, Json& j);
    const uint8_t* consume_number(const uint8_t* first, const uint8_t* last, const String& str, bool complete, Json& j);
};

inline Json::streaming_parser::streaming_parser()
    : state_(st_initial) {
}

inline void Json::streaming_parser::reset() {
    state_ = st_initial;
    stack_.clear();
}

inline bool Json::streaming_parser::done() const {
    return state_ < 0;
}

inline bool Json::streaming_parser::success() const {
    return state_ == st_final;
}

inline bool Json::streaming_parser::error() const {
    return state_ == st_error;
}

inline size_t Json::streaming_parser::consume(const char* first, size_t length,
                                              const String& str,
                                              bool complete) {
    const uint8_t* ufirst = reinterpret_cast<const uint8_t*>(first);
    return consume(ufirst, ufirst + length, str, complete) - ufirst;
}

inline const char* Json::streaming_parser::consume(const char* first,
                                                   const char* last,
                                                   const String& str,
                                                   bool complete) {
    return reinterpret_cast<const char*>
        (consume(reinterpret_cast<const uint8_t*>(first),
                 reinterpret_cast<const uint8_t*>(last), str, complete));
}

inline Json& Json::streaming_parser::result() {
    return json_;
}

inline const Json& Json::streaming_parser::result() const {
    return json_;
}


/** @brief Parse @a str as UTF-8 JSON into this Json object.
    @return true iff the parse succeeded.

    An unsuccessful parse does not modify *this. */
inline bool Json::assign_parse(const String &str) {
    return assign_parse(str.begin(), str.end(), str);
}

/** @brief Parse [@a first, @a last) as UTF-8 JSON into this Json object.
    @return true iff the parse succeeded.

    An unsuccessful parse does not modify *this. */
inline bool Json::assign_parse(const char *first, const char *last) {
    return assign_parse(first, last, String());
}

/** @brief Return @a str parsed as UTF-8 JSON.

    Returns a null JSON object if the parse fails. */
inline Json Json::parse(const String &str) {
    Json j;
    (void) j.assign_parse(str);
    return j;
}

/** @brief Return [@a first, @a last) parsed as UTF-8 JSON.

    Returns a null JSON object if the parse fails. */
inline Json Json::parse(const char *first, const char *last) {
    Json j;
    (void) j.assign_parse(first, last);
    return j;
}


// Assignment

inline Json& Json::operator=(const Json& x) {
    if (x.u_.x.type < 0)
        x.u_.str.ref();
    else if (x.u_.x.x && (x.u_.x.type == j_array || x.u_.x.type == j_object))
        x.u_.x.x->ref();
    deref();
    u_ = x.u_;
    return *this;
}

#if HAVE_CXX_RVALUE_REFERENCES
inline Json& Json::operator=(Json&& x) {
    using std::swap;
    swap(u_, x.u_);
    return *this;
}
#endif

/** @cond never */
template <typename U>
inline Json& Json::operator=(const Json_proxy_base<U> &x) {
    return *this = x.cvalue();
}

inline Json& Json::operator=(int x) {
    deref();
    u_.i.x = x;
    u_.i.type = j_int;
    return *this;
}

inline Json& Json::operator=(unsigned x) {
    deref();
    u_.u.x = x;
    u_.u.type = j_unsigned;
    return *this;
}

inline Json& Json::operator=(long x) {
    deref();
    u_.i.x = x;
    u_.i.type = j_int;
    return *this;
}

inline Json& Json::operator=(unsigned long x) {
    deref();
    u_.u.x = x;
    u_.u.type = j_unsigned;
    return *this;
}

inline Json& Json::operator=(long long x) {
    deref();
    u_.i.x = x;
    u_.i.type = j_int;
    return *this;
}

inline Json& Json::operator=(unsigned long long x) {
    deref();
    u_.u.x = x;
    u_.u.type = j_unsigned;
    return *this;
}

inline Json& Json::operator=(double x) {
    deref();
    u_.d.x = x;
    u_.d.type = j_double;
    return *this;
}

inline Json& Json::operator=(bool x) {
    deref();
    u_.i.x = x;
    u_.i.type = j_bool;
    return *this;
}

inline Json& Json::operator=(const String& x) {
    deref();
    u_.str = x.internal_rep();
    u_.str.ref();
    return *this;
}
/** @endcond never */

inline Json& Json::operator++() {
    return *this += 1;
}
inline void Json::operator++(int) {
    ++(*this);
}
inline Json& Json::operator--() {
    return *this += -1;
}
inline void Json::operator--(int) {
    --(*this);
}
inline Json& Json::add(double x) {
    force_double();
    u_.d.x += x;
    return *this;
}
template <typename T>
inline Json& Json::add(T x) {
    force_number();
    if (is_int())
        u_.u.x += x;
    else
        u_.d.x += x;
    return *this;
}
inline Json& Json::subtract(double x) {
    force_double();
    u_.d.x -= x;
    return *this;
}
template <typename T>
inline Json& Json::subtract(T x) {
    force_number();
    if (is_int())
        u_.u.x -= x;
    else
        u_.d.x -= x;
    return *this;
}
inline Json& Json::operator+=(int x) {
    return add(x);
}
inline Json& Json::operator+=(unsigned x) {
    return add(x);
}
inline Json& Json::operator+=(long x) {
    return add(x);
}
inline Json& Json::operator+=(unsigned long x) {
    return add(x);
}
inline Json& Json::operator+=(long long x) {
    return add(x);
}
inline Json& Json::operator+=(unsigned long long x) {
    return add(x);
}
inline Json& Json::operator+=(double x) {
    return add(x);
}
inline Json& Json::operator-=(int x) {
    return subtract(x);
}
inline Json& Json::operator-=(unsigned x) {
    return subtract(x);
}
inline Json& Json::operator-=(long x) {
    return subtract(x);
}
inline Json& Json::operator-=(unsigned long x) {
    return subtract(x);
}
inline Json& Json::operator-=(long long x) {
    return subtract(x);
}
inline Json& Json::operator-=(unsigned long long x) {
    return subtract(x);
}
inline Json& Json::operator-=(double x) {
    return subtract(x);
}
inline Json& Json::operator+=(const Json& x) {
    if (x.is_unsigned())
        add(u_.u.x);
    else if (x.is_int())
        add(u_.i.x);
    else if (!x.is_null())
        add(x.as_d());
    return *this;
}
inline Json& Json::operator-=(const Json& x) {
    if (x.is_unsigned())
        subtract(u_.u.x);
    else if (x.is_int())
        subtract(u_.i.x);
    else if (!x.is_null())
        subtract(x.as_d());
    return *this;
}
inline Json operator+(Json x) {
    x.force_number();
    return x;
}
inline Json operator-(Json x) {
    x.force_number();
    if (x.is_int())
        x.u_.u.x = -x.u_.u.x;
    else
        x.u_.d.x = -x.u_.d.x;
    return x;
}

/** @brief Swap this Json with @a x. */
inline void Json::swap(Json& x) {
    using std::swap;
    swap(u_, x.u_);
}


inline StringAccum &operator<<(StringAccum &sa, const Json& json) {
    json.unparse(sa);
    return sa;
}

template <typename P>
inline StringAccum &operator<<(StringAccum &sa, const Json_proxy_base<P> &json) {
    return (sa << json.cvalue());
}

inline std::ostream &operator<<(std::ostream& f, const Json& json) {
    StringAccum sa;
    json.unparse(sa);
    return f.write(sa.data(), sa.length());
}

template <typename P>
inline std::ostream &operator<<(std::ostream& f, const Json_proxy_base<P>& json) {
    return (f << json.cvalue());
}

bool operator==(const Json& a, const Json& b);

template <typename T>
inline bool operator==(const Json_proxy_base<T>& a, const Json& b) {
    return a.cvalue() == b;
}

template <typename T>
inline bool operator==(const Json& a, const Json_proxy_base<T>& b) {
    return a == b.cvalue();
}

template <typename T, typename U>
inline bool operator==(const Json_proxy_base<T>& a,
                       const Json_proxy_base<U>& b) {
    return a.cvalue() == b.cvalue();
}

inline bool operator!=(const Json& a, const Json& b) {
    return !(a == b);
}

template <typename T>
inline bool operator!=(const Json_proxy_base<T>& a, const Json& b) {
    return !(a == b);
}

template <typename T>
inline bool operator!=(const Json& a, const Json_proxy_base<T>& b) {
    return !(a == b);
}

template <typename T, typename U>
inline bool operator!=(const Json_proxy_base<T>& a,
                       const Json_proxy_base<U>& b) {
    return !(a == b);
}

inline void swap(Json& a, Json& b) {
    a.swap(b);
}

} // namespace lcdf
#endif

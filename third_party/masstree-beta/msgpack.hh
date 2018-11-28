#ifndef GSTORE_MSGPACK_HH
#define GSTORE_MSGPACK_HH
#include "json.hh"
#include "small_vector.hh"
#include "straccum.hh"
#include <vector>
namespace msgpack {
using lcdf::Json;
using lcdf::Str;
using lcdf::String;
using lcdf::StringAccum;

namespace format {
enum {
    ffixuint = 0x00, nfixuint = 0x80,
    ffixmap = 0x80, nfixmap = 0x10,
    ffixarray = 0x90, nfixarray = 0x10,
    ffixstr = 0xA0, nfixstr = 0x20,
    fnull = 0xC0,
    ffalse = 0xC2, ftrue = 0xC3,
    fbin8 = 0xC4, fbin16 = 0xC5, fbin32 = 0xC6,
    fext8 = 0xC7, fext16 = 0xC8, fext32 = 0xC9,
    ffloat32 = 0xCA, ffloat64 = 0xCB,
    fuint8 = 0xCC, fuint16 = 0xCD, fuint32 = 0xCE, fuint64 = 0xCF,
    fint8 = 0xD0, fint16 = 0xD1, fint32 = 0xD2, fint64 = 0xD3,
    ffixext1 = 0xD4, ffixext2 = 0xD5, ffixext4 = 0xD6,
    ffixext8 = 0xD7, ffixext16 = 0xD8,
    fstr8 = 0xD9, fstr16 = 0xDA, fstr32 = 0xDB,
    farray16 = 0xDC, farray32 = 0xDD,
    fmap16 = 0xDE, fmap32 = 0xDF,
    ffixnegint = 0xE0, nfixnegint = 0x20,
    nfixint = nfixuint + nfixnegint
};

inline bool in_range(uint8_t x, unsigned low, unsigned n) {
    return (unsigned) x - low < n;
}
inline bool in_wrapped_range(uint8_t x, unsigned low, unsigned n) {
    return (unsigned) (int8_t) x - low < n;
}

inline bool is_fixint(uint8_t x) {
    return in_wrapped_range(x, -nfixnegint, nfixint);
}
inline bool is_null_or_bool(uint8_t x) {
    return in_range(x, fnull, 4);
}
inline bool is_bool(uint8_t x) {
    return in_range(x, ffalse, 2);
}
inline bool is_fixstr(uint8_t x) {
    return in_range(x, ffixstr, nfixstr);
}
inline bool is_fixarray(uint8_t x) {
    return in_range(x, ffixarray, nfixarray);
}
inline bool is_fixmap(uint8_t x) {
    return in_range(x, ffixmap, nfixmap);
}

inline char* write_null(char* s) {
    *s++ = fnull;
    return s;
}
inline char* write_bool(char* s, bool x) {
    *s++ = ffalse + x;
    return s;
}

template <size_t s> struct sized_writer {};
template <> struct sized_writer<4> {
    static inline char* write_unsigned(char* s, uint32_t x) {
        if (x < nfixuint)
            *s++ = x;
        else if (x < 256) {
            *s++ = fuint8;
            *s++ = x;
        } else if (x < 65536) {
            *s++ = fuint16;
            s = write_in_net_order<uint16_t>(s, (uint16_t) x);
        } else {
            *s++ = fuint32;
            s = write_in_net_order<uint32_t>(s, x);
        }
        return s;
    }
    static inline char* write_signed(char* s, int32_t x) {
        if ((uint32_t) x + nfixnegint < nfixint)
            *s++ = x;
        else if ((uint32_t) x + 128 < 256) {
            *s++ = fint8;
            *s++ = x;
        } else if ((uint32_t) x + 32768 < 65536) {
            *s++ = fint16;
            s = write_in_net_order<int16_t>(s, (int16_t) x);
        } else {
            *s++ = fint32;
            s = write_in_net_order<int32_t>(s, x);
        }
        return s;
    }
};
template <> struct sized_writer<8> {
    static inline char* write_unsigned(char* s, uint64_t x) {
        if (x < 4294967296ULL)
            return sized_writer<4>::write_unsigned(s, (uint32_t) x);
        else {
            *s++ = fuint64;
            return write_in_net_order<uint64_t>(s, x);
        }
    }
    static inline char* write_signed(char* s, int64_t x) {
        if (x + 2147483648ULL < 4294967296ULL)
            return sized_writer<4>::write_signed(s, (int32_t) x);
        else {
            *s++ = fint64;
            return write_in_net_order<int64_t>(s, x);
        }
    }
};
inline char* write_int(char* s, int x) {
    return sized_writer<sizeof(x)>::write_signed(s, x);
}
inline char* write_int(char* s, unsigned x) {
    return sized_writer<sizeof(x)>::write_unsigned(s, x);
}
inline char* write_int(char* s, long x) {
    return sized_writer<sizeof(x)>::write_signed(s, x);
}
inline char* write_int(char* s, unsigned long x) {
    return sized_writer<sizeof(x)>::write_unsigned(s, x);
}
#if HAVE_LONG_LONG && SIZEOF_LONG_LONG <= 8
inline char* write_int(char* s, long long x) {
    return sized_writer<sizeof(x)>::write_signed(s, x);
}
inline char* write_int(char* s, unsigned long long x) {
    return sized_writer<sizeof(x)>::write_unsigned(s, x);
}
#endif
inline char* write_wide_int64(char* s, uint64_t x) {
    *s++ = fuint64;
    return write_in_net_order<uint64_t>(s, x);
}
inline char* write_wide_int64(char* s, int64_t x) {
    *s++ = fint64;
    return write_in_net_order<int64_t>(s, x);
}
inline char* write_float(char* s, float x) {
    *s++ = ffloat32;
    return write_in_net_order<float>(s, x);
}
inline char* write_double(char* s, double x) {
    *s++ = ffloat64;
    return write_in_net_order<double>(s, x);
}
inline char* write_string(char* s, const char *data, int len) {
    if (len < nfixstr)
        *s++ = 0xA0 + len;
    else if (len < 256) {
        *s++ = fstr8;
        *s++ = len;
    } else if (len < 65536) {
        *s++ = fstr16;
        s = write_in_net_order<uint16_t>(s, (uint16_t) len);
    } else {
        *s++ = fstr32;
        s = write_in_net_order<uint32_t>(s, len);
    }
    memcpy(s, data, len);
    return s + len;
}
inline char* write_string(char* s, Str x) {
    return write_string(s, x.data(), x.length());
}
template <typename T>
inline char* write_string(char* s, const lcdf::String_base<T>& x) {
    return write_string(s, x.data(), x.length());
}
inline char* write_array_header(char* s, uint32_t size) {
    if (size < nfixarray) {
        *s++ = ffixarray + size;
        return s;
    } else if (size < 65536) {
        *s++ = farray16;
        return write_in_net_order<uint16_t>(s, (uint16_t) size);
    } else {
        *s++ = farray32;
        return write_in_net_order<uint32_t>(s, (uint32_t) size);
    }
}
inline char* write_map_header(char* s, uint32_t size) {
    if (size < nfixmap) {
        *s++ = ffixmap + size;
        return s;
    } else if (size < 65536) {
        *s++ = fmap16;
        return write_in_net_order<uint16_t>(s, (uint16_t) size);
    } else {
        *s++ = fmap32;
        return write_in_net_order<uint32_t>(s, (uint32_t) size);
    }
}
} // namespace format

struct array_t {
    uint32_t size;
    array_t(uint32_t s)
        : size(s) {
    }
};

inline array_t array(uint32_t size) {
    return array_t(size);
}

struct object_t {
    uint32_t size;
    object_t(uint32_t s)
        : size(s) {
    }
};

inline object_t object(uint32_t size) {
    return object_t(size);
}

template <typename T>
class unparser {
  public:
    inline unparser(T& base)
        : base_(base) {
    }
    template <typename X>
    inline unparser(T& base, const X& x)
        : base_(base) {
        *this << x;
    }

    inline void clear() {
        base_.clear();
    }

    inline unparser<T>& null() {
        base_.append((char) format::fnull);
        return *this;
    }
    inline unparser<T>& operator<<(const Json::null_t&) {
        return null();
    }
    inline unparser<T>& operator<<(int x) {
        char* s = base_.reserve(sizeof(x) + 1);
        base_.set_end(format::write_int(s, x));
        return *this;
    }
    inline unparser<T>& operator<<(unsigned x) {
        char* s = base_.reserve(sizeof(x) + 1);
        base_.set_end(format::write_int(s, x));
        return *this;
    }
    inline unparser<T>& operator<<(long x) {
        char* s = base_.reserve(sizeof(x) + 1);
        base_.set_end(format::write_int(s, x));
        return *this;
    }
    inline unparser<T>& operator<<(unsigned long x) {
        char* s = base_.reserve(sizeof(x) + 1);
        base_.set_end(format::write_int(s, x));
        return *this;
    }
#if HAVE_LONG_LONG && SIZEOF_LONG_LONG <= 8
    inline unparser<T>& operator<<(long long x) {
        char* s = base_.reserve(sizeof(x) + 1);
        base_.set_end(format::write_int(s, x));
        return *this;
    }
    inline unparser<T>& operator<<(unsigned long long x) {
        char* s = base_.reserve(sizeof(x) + 1);
        base_.set_end(format::write_int(s, x));
        return *this;
    }
#endif
    inline unparser<T>& write_wide(int64_t x) {
        char* s = base_.reserve(sizeof(x) + 1);
        base_.set_end(format::write_wide_int64(s, x));
        return *this;
    }
    inline unparser<T>& write_wide(uint64_t x) {
        char* s = base_.reserve(sizeof(x) + 1);
        base_.set_end(format::write_wide_int64(s, x));
        return *this;
    }
    inline unparser<T>& operator<<(float x) {
        char* s = base_.reserve(sizeof(x) + 1);
        base_.set_end(format::write_float(s, x));
        return *this;
    }
    inline unparser<T>& operator<<(double x) {
        char* s = base_.reserve(sizeof(x) + 1);
        base_.set_end(format::write_double(s, x));
        return *this;
    }
    inline unparser<T>& operator<<(Str x) {
        char* s = base_.reserve(5 + x.length());
        base_.set_end(format::write_string(s, x.data(), x.length()));
        return *this;
    }
    template <typename X>
    inline unparser<T>& operator<<(const lcdf::String_base<X>& x) {
        char* s = base_.reserve(5 + x.length());
        base_.set_end(format::write_string(s, x.data(), x.length()));
        return *this;
    }
    inline unparser<T>& operator<<(array_t x) {
        char* s = base_.reserve(5);
        base_.set_end(format::write_array_header(s, x.size));
        return *this;
    }
    inline unparser<T>& write_array_header(uint32_t size) {
        char* s = base_.reserve(5);
        base_.set_end(format::write_array_header(s, size));
        return *this;
    }
    inline unparser<T>& operator<<(object_t x) {
        char* s = base_.reserve(5);
        base_.set_end(format::write_map_header(s, x.size));
        return *this;
    }
    unparser<T>& operator<<(const Json& j);
    template <typename X>
    inline unparser<T>& write(const X& x) {
        return *this << x;
    }

  private:
    T& base_;
};

class streaming_parser {
  public:
    inline streaming_parser();
    inline void reset();

    inline bool empty() const;
    inline bool done() const;
    inline bool success() const;
    inline bool error() const;

    inline size_t consume(const char* first, size_t length,
                          const String& str = String());
    inline const char* consume(const char* first, const char* last,
                               const String& str = String());
    const uint8_t* consume(const uint8_t* first, const uint8_t* last,
                           const String& str = String());

    inline Json& result();
    inline const Json& result() const;

  private:
    enum {
        st_final = -2, st_error = -1, st_normal = 0, st_partial = 1,
        st_string = 2
    };
    struct selem {
        Json* jp;
        int size;
    };
    int state_;
    small_vector<selem, 2> stack_;
    String str_;
    Json json_;
    Json jokey_;
};

class parser {
  public:
    explicit inline parser(const char* s)
        : s_(reinterpret_cast<const unsigned char*>(s)), str_() {
    }
    explicit inline parser(const unsigned char* s)
        : s_(s), str_() {
    }
    explicit inline parser(const String& str)
        : s_(reinterpret_cast<const uint8_t*>(str.begin())), str_(str) {
    }
    inline const char* position() const {
        return reinterpret_cast<const char*>(s_);
    }

    inline bool try_read_null() {
        if (*s_ == format::fnull) {
            ++s_;
            return true;
        } else
            return false;
    }
    inline int read_tiny_int() {
        assert(format::is_fixint(*s_));
        return (int8_t) *s_++;
    }
    inline parser& read_tiny_int(int& x) {
        x = read_tiny_int();
        return *this;
    }
    template <typename T>
    inline parser& read_int(T& x) {
        if (format::is_fixint(*s_)) {
            x = (int8_t) *s_;
            ++s_;
        } else {
            assert((uint32_t) *s_ - format::fuint8 < 8);
            hard_read_int(x);
        }
        return *this;
    }
    inline parser& operator>>(int& x) {
        return read_int(x);
    }
    inline parser& operator>>(long& x) {
        return read_int(x);
    }
    inline parser& operator>>(long long& x) {
        return read_int(x);
    }
    inline parser& operator>>(unsigned& x) {
        return read_int(x);
    }
    inline parser& operator>>(unsigned long& x) {
        return read_int(x);
    }
    inline parser& operator>>(unsigned long long& x) {
        return read_int(x);
    }
    inline parser& operator>>(bool& x) {
        assert(format::is_bool(*s_));
        x = *s_ - format::ffalse;
        ++s_;
        return *this;
    }
    inline parser& operator>>(double& x) {
        assert(*s_ == format::ffloat64);
        x = read_in_net_order<double>(s_ + 1);
        s_ += 9;
        return *this;
    }
    parser& operator>>(Str& x);
    parser& operator>>(String& x);
    inline parser& read_array_header(unsigned& size) {
        if (format::is_fixarray(*s_)) {
            size = *s_ - format::ffixarray;
            s_ += 1;
        } else if (*s_ == format::farray16) {
            size = read_in_net_order<uint16_t>(s_ + 1);
            s_ += 3;
        } else {
            assert(*s_ == format::farray32);
            size = read_in_net_order<uint32_t>(s_ + 1);
            s_ += 5;
        }
        return *this;
    }
    template <typename T> parser& operator>>(::std::vector<T>& x);
    inline parser& operator>>(Json& j);

    inline parser& skip_primitives(unsigned n) {
        for (; n != 0; --n) {
            if (format::is_fixint(*s_) || format::is_null_or_bool(*s_))
                s_ += 1;
            else if (format::is_fixstr(*s_))
                s_ += 1 + (*s_ - format::ffixstr);
            else if (*s_ == format::fuint8 || *s_ == format::fint8)
                s_ += 2;
            else if (*s_ == format::fuint16 || *s_ == format::fint16)
                s_ += 3;
            else if (*s_ == format::fuint32 || *s_ == format::fint32
                     || *s_ == format::ffloat32)
                s_ += 5;
            else if (*s_ == format::fstr8 || *s_ == format::fbin8)
                s_ += 2 + s_[1];
            else if (*s_ == format::fstr16 || *s_ == format::fbin16)
                s_ += 3 + read_in_net_order<uint16_t>(s_ + 1);
            else if (*s_ == format::fstr32 || *s_ == format::fbin32)
                s_ += 5 + read_in_net_order<uint32_t>(s_ + 1);
        }
        return *this;
    }
    inline parser& skip_primitive() {
        return skip_primitives(1);
    }
    inline parser& skip_array_size() {
        if (format::is_fixarray(*s_))
            s_ += 1;
        else if (*s_ == format::farray16)
            s_ += 3;
        else {
            assert(*s_ == format::farray32);
            s_ += 5;
        }
        return *this;
    }
  private:
    const uint8_t* s_;
    String str_;
    template <typename T> void hard_read_int(T& x);
};

template <typename T>
void parser::hard_read_int(T& x) {
    switch (*s_) {
    case format::fuint8:
        x = s_[1];
        s_ += 2;
        break;
    case format::fuint16:
        x = read_in_net_order<uint16_t>(s_ + 1);
        s_ += 3;
        break;
    case format::fuint32:
        x = read_in_net_order<uint32_t>(s_ + 1);
        s_ += 5;
        break;
    case format::fuint64:
        x = read_in_net_order<uint64_t>(s_ + 1);
        s_ += 9;
        break;
    case format::fint8:
        x = (int8_t) s_[1];
        s_ += 2;
        break;
    case format::fint16:
        x = read_in_net_order<int16_t>(s_ + 1);
        s_ += 3;
        break;
    case format::fint32:
        x = read_in_net_order<int32_t>(s_ + 1);
        s_ += 5;
        break;
    case format::fint64:
        x = read_in_net_order<int64_t>(s_ + 1);
        s_ += 9;
        break;
    }
}

template <typename T>
unparser<T>& unparser<T>::operator<<(const Json& j) {
    if (j.is_null())
        base_.append(char(format::fnull));
    else if (j.is_b())
        base_.append(char(format::ffalse + j.as_b()));
    else if (j.is_u()) {
        char* x = base_.reserve(9);
        base_.set_end(format::write_int(x, j.as_u()));
    } else if (j.is_i()) {
        char* x = base_.reserve(9);
        base_.set_end(format::write_int(x, j.as_i()));
    } else if (j.is_d()) {
        char* x = base_.reserve(9);
        base_.set_end(format::write_double(x, j.as_d()));
    } else if (j.is_s()) {
        char* x = base_.reserve(j.as_s().length() + 5);
        base_.set_end(format::write_string(x, j.as_s()));
    } else if (j.is_a()) {
        char* x = base_.reserve(5);
        base_.set_end(format::write_array_header(x, j.size()));
        for (auto it = j.cabegin(); it != j.caend(); ++it)
            *this << *it;
    } else if (j.is_o()) {
        char* x = base_.reserve(5);
        base_.set_end(format::write_map_header(x, j.size()));
        for (auto it = j.cobegin(); it != j.coend(); ++it) {
            char* x = base_.reserve(it.key().length() + 5);
            base_.set_end(format::write_string(x, it.key()));
            *this << it.value();
        }
    } else
        base_.append(char(format::fnull));
    return *this;
}

inline String unparse(const Json& j) {
    StringAccum sa;
    unparser<StringAccum>(sa, j);
    return sa.take_string();
}
template <typename T, typename X>
inline T& unparse(T& s, const X& x) {
    unparser<T>(s) << x;
    return s;
}
template <typename T, typename X>
inline T& unparse_wide(T& s, const X& x) {
    unparser<T>(s).write_wide(x);
    return s;
}

template <typename T>
parser& parser::operator>>(::std::vector<T>& x) {
    uint32_t sz;
    if ((uint32_t) *s_ - format::ffixarray < format::nfixarray) {
        sz = *s_ - format::ffixarray;
        ++s_;
    } else if (*s_ == format::farray16) {
        sz = read_in_net_order<uint16_t>(s_ + 1);
        s_ += 3;
    } else {
        assert(*s_ == format::farray32);
        sz = read_in_net_order<uint32_t>(s_ + 1);
        s_ += 5;
    }
    for (; sz != 0; --sz) {
        x.push_back(T());
        parse(x.back());
    }
    return *this;
}

inline streaming_parser::streaming_parser()
    : state_(st_normal) {
}

inline void streaming_parser::reset() {
    state_ = st_normal;
    stack_.clear();
}

inline bool streaming_parser::empty() const {
    return state_ == st_normal && stack_.empty();
}

inline bool streaming_parser::done() const {
    return state_ < 0;
}

inline bool streaming_parser::success() const {
    return state_ == st_final;
}

inline bool streaming_parser::error() const {
    return state_ == st_error;
}

inline const char* streaming_parser::consume(const char* first,
                                             const char* last,
                                             const String& str) {
    return reinterpret_cast<const char*>
        (consume(reinterpret_cast<const uint8_t*>(first),
                 reinterpret_cast<const uint8_t*>(last), str));
}

inline size_t streaming_parser::consume(const char* first, size_t length,
                                        const String& str) {
    const uint8_t* ufirst = reinterpret_cast<const uint8_t*>(first);
    return consume(ufirst, ufirst + length, str) - ufirst;
}

inline Json& streaming_parser::result() {
    return json_;
}

inline const Json& streaming_parser::result() const {
    return json_;
}

inline parser& parser::operator>>(Json& j)  {
    using std::swap;
    streaming_parser sp;
    while (!sp.done())
        s_ = sp.consume(s_, s_ + 4096, str_);
    if (sp.success())
        swap(j, sp.result());
    return *this;
}

inline Json parse(const char* first, const char* last) {
    streaming_parser sp;
    first = sp.consume(first, last, String());
    if (sp.success())
        return sp.result();
    return Json();
}

inline Json parse(const String& str) {
    streaming_parser sp;
    sp.consume(str.begin(), str.end(), str);
    if (sp.success())
        return sp.result();
    return Json();
}

} // namespace msgpack
#endif

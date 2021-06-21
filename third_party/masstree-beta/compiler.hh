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
#ifndef MASSTREE_COMPILER_HH
#define MASSTREE_COMPILER_HH 1
#include <stdint.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <arpa/inet.h>
#if HAVE_TYPE_TRAITS
#include <type_traits>
#endif

#define arraysize(a) (sizeof(a) / sizeof((a)[0]))

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#if HAVE_OFF_T_IS_LONG_LONG
#define PRIdOFF_T "lld"
#else
#define PRIdOFF_T "ld"
#endif

#if HAVE_SIZE_T_IS_UNSIGNED_LONG_LONG
#define PRIdSIZE_T "llu"
#define PRIdSSIZE_T "lld"
#elif HAVE_SIZE_T_IS_UNSIGNED_LONG
#define PRIdSIZE_T "lu"
#define PRIdSSIZE_T "ld"
#else
#define PRIdSIZE_T "u"
#define PRIdSSIZE_T "d"
#endif

#if (__i386__ || __x86_64__) && !defined(__x86__)
# define __x86__ 1
#endif
#define PREFER_X86 1
#define ALLOW___SYNC_BUILTINS 1

#if !defined(HAVE_INDIFFERENT_ALIGMENT) && (__i386__ || __x86_64__ || __arch_um__)
# define HAVE_INDIFFERENT_ALIGNMENT 1
#endif


/** @brief Return the index of the most significant bit set in @a x.
 * @return 0 if @a x = 0; otherwise the index of first bit set, where the
 * most significant bit is numbered 1.
 */
inline int ffs_msb(unsigned x) {
    return (x ? __builtin_clz(x) + 1 : 0);
}
/** @overload */
inline int ffs_msb(unsigned long x) {
    return (x ? __builtin_clzl(x) + 1 : 0);
}
/** @overload */
inline int ffs_msb(unsigned long long x) {
    return (x ? __builtin_clzll(x) + 1 : 0);
}


/** @brief Compiler fence.
 *
 * Prevents reordering of loads and stores by the compiler. Not intended to
 * synchronize the processor's caches. */
inline void fence() {
    asm volatile("" : : : "memory");
}

/** @brief Acquire fence. */
inline void acquire_fence() {
    asm volatile("" : : : "memory");
}

/** @brief Release fence. */
inline void release_fence() {
    asm volatile("" : : : "memory");
}

/** @brief Compiler fence that relaxes the processor.

    Use this in spinloops, for example. */
inline void relax_fence() {
    asm volatile("pause" : : : "memory"); // equivalent to "rep; nop"
}

/** @brief Full memory fence. */
inline void memory_fence() {
    asm volatile("mfence" : : : "memory");
}

/** @brief Do-nothing function object. */
struct do_nothing {
    void operator()() const {
    }
    template <typename T>
    void operator()(const T&) const {
    }
    template <typename T, typename U>
    void operator()(const T&, const U&) const {
    }
};

/** @brief Function object that calls fence(). */
struct fence_function {
    void operator()() const {
        fence();
    }
};

/** @brief Function object that calls relax_fence(). */
struct relax_fence_function {
    void operator()() const {
        relax_fence();
    }
};

/** @brief Function object that calls relax_fence() with backoff. */
struct backoff_fence_function {
    backoff_fence_function()
        : count_(0) {
    }
    void operator()() {
        for (int i = count_; i >= 0; --i)
            relax_fence();
        count_ = ((count_ << 1) | 1) & 15;
    }
  private:
    int count_;
};


template <int SIZE, typename BARRIER> struct sized_compiler_operations;

template <typename B> struct sized_compiler_operations<1, B> {
    typedef char type;
    static inline type xchg(type* object, type new_value) {
        asm volatile("xchgb %0,%1"
                     : "+q" (new_value), "+m" (*object));
        B()();
        return new_value;
    }
    static inline type val_cmpxchg(type* object, type expected, type desired) {
#if __x86__ && (PREFER_X86 || !HAVE___SYNC_VAL_COMPARE_AND_SWAP)
        asm volatile("lock; cmpxchgb %2,%1"
                     : "+a" (expected), "+m" (*object)
                     : "r" (desired) : "cc");
        B()();
        return expected;
#else
        return __sync_val_compare_and_swap(object, expected, desired);
#endif
    }
    static inline bool bool_cmpxchg(type* object, type expected, type desired) {
#if HAVE___SYNC_BOOL_COMPARE_AND_SWAP && ALLOW___SYNC_BUILTINS
        return __sync_bool_compare_and_swap(object, expected, desired);
#else
        bool result;
        asm volatile("lock; cmpxchgb %3,%1; sete %b2"
                     : "+a" (expected), "+m" (*object), "=q" (result)
                     : "q" (desired) : "cc");
        B()();
        return result;
#endif
    }
    static inline type fetch_and_add(type *object, type addend) {
#if __x86__ && (PREFER_X86 || !HAVE___SYNC_FETCH_AND_ADD)
        asm volatile("lock; xaddb %0,%1"
                     : "+q" (addend), "+m" (*object) : : "cc");
        B()();
        return addend;
#else
        return __sync_fetch_and_add(object, addend);
#endif
    }
    static inline void atomic_or(type* object, type addend) {
#if __x86__
        asm volatile("lock; orb %0,%1"
                     : "=r" (addend), "+m" (*object) : : "cc");
        B()();
#else
        __sync_fetch_and_or(object, addend);
#endif
    }
};

template <typename B> struct sized_compiler_operations<2, B> {
#if SIZEOF_SHORT == 2
    typedef short type;
#else
    typedef int16_t type;
#endif
    static inline type xchg(type* object, type new_value) {
        asm volatile("xchgw %0,%1"
                     : "+r" (new_value), "+m" (*object));
        B()();
        return new_value;
    }
    static inline type val_cmpxchg(type* object, type expected, type desired) {
#if __x86__ && (PREFER_X86 || !HAVE___SYNC_VAL_COMPARE_AND_SWAP)
        asm volatile("lock; cmpxchgw %2,%1"
                     : "+a" (expected), "+m" (*object)
                     : "r" (desired) : "cc");
        B()();
        return expected;
#else
        return __sync_val_compare_and_swap(object, expected, desired);
#endif
    }
    static inline bool bool_cmpxchg(type* object, type expected, type desired) {
#if HAVE___SYNC_BOOL_COMPARE_AND_SWAP && ALLOW___SYNC_BUILTINS
        return __sync_bool_compare_and_swap(object, expected, desired);
#else
        bool result;
        asm volatile("lock; cmpxchgw %3,%1; sete %b2"
                     : "+a" (expected), "+m" (*object), "=q" (result)
                     : "r" (desired) : "cc");
        B()();
        return result;
#endif
    }
    static inline type fetch_and_add(type* object, type addend) {
#if __x86__ && (PREFER_X86 || !HAVE___SYNC_FETCH_AND_ADD)
        asm volatile("lock; xaddw %0,%1"
                     : "+r" (addend), "+m" (*object) : : "cc");
        B()();
        return addend;
#else
        return __sync_fetch_and_add(object, addend);
#endif
    }
    static inline void atomic_or(type* object, type addend) {
#if __x86__
        asm volatile("lock; orw %0,%1"
                     : "=r" (addend), "+m" (*object) : : "cc");
        B()();
#else
        __sync_fetch_and_or(object, addend);
#endif
    }
};

template <typename B> struct sized_compiler_operations<4, B> {
#if SIZEOF_INT == 4
    typedef int type;
#else
    typedef int32_t type;
#endif
    static inline type xchg(type* object, type new_value) {
        asm volatile("xchgl %0,%1"
                     : "+r" (new_value), "+m" (*object));
        B()();
        return new_value;
    }
    static inline type val_cmpxchg(type* object, type expected, type desired) {
#if __x86__ && (PREFER_X86 || !HAVE___SYNC_VAL_COMPARE_AND_SWAP)
        asm volatile("lock; cmpxchgl %2,%1"
                     : "+a" (expected), "+m" (*object)
                     : "r" (desired) : "cc");
        B()();
        return expected;
#else
        return __sync_val_compare_and_swap(object, expected, desired);
#endif
    }
    static inline bool bool_cmpxchg(type* object, type expected, type desired) {
#if HAVE___SYNC_BOOL_COMPARE_AND_SWAP && ALLOW___SYNC_BUILTINS
        return __sync_bool_compare_and_swap(object, expected, desired);
#else
        bool result;
        asm volatile("lock; cmpxchgl %3,%1; sete %b2"
                     : "+a" (expected), "+m" (*object), "=q" (result)
                     : "r" (desired) : "cc");
        B()();
        return result;
#endif
    }
    static inline type fetch_and_add(type *object, type addend) {
#if __x86__ && (PREFER_X86 || !HAVE___SYNC_FETCH_AND_ADD)
        asm volatile("lock; xaddl %0,%1"
                     : "+r" (addend), "+m" (*object) : : "cc");
        B()();
        return addend;
#else
        return __sync_fetch_and_add(object, addend);
#endif
    }
    static inline void atomic_or(type* object, type addend) {
#if __x86__
        asm volatile("lock; orl %0,%1"
                     : "=r" (addend), "+m" (*object) : : "cc");
        B()();
#else
        __sync_fetch_and_or(object, addend);
#endif
    }
};

template <typename B> struct sized_compiler_operations<8, B> {
#if SIZEOF_LONG_LONG == 8
    typedef long long type;
#elif SIZEOF_LONG == 8
    typedef long type;
#else
    typedef int64_t type;
#endif
#if __x86_64__
    static inline type xchg(type* object, type new_value) {
        asm volatile("xchgq %0,%1"
                     : "+r" (new_value), "+m" (*object));
        B()();
        return new_value;
    }
#endif
    static inline type val_cmpxchg(type* object, type expected, type desired) {
#if __x86_64__ && (PREFER_X86 || !HAVE___SYNC_VAL_COMPARE_AND_SWAP_8)
        asm volatile("lock; cmpxchgq %2,%1"
                     : "+a" (expected), "+m" (*object)
                     : "r" (desired) : "cc");
        B()();
        return expected;
#elif __i386__ && (PREFER_X86 || !HAVE___SYNC_VAL_COMPARE_AND_SWAP_8)
        uint32_t expected_low(expected), expected_high(expected >> 32),
            desired_low(desired), desired_high(desired >> 32);
        asm volatile("lock; cmpxchg8b %2"
                     : "+a" (expected_low), "+d" (expected_high), "+m" (*object)
                     : "b" (desired_low), "c" (desired_high) : "cc");
        B()();
        return ((uint64_t) expected_high << 32) | expected_low;
#elif HAVE___SYNC_VAL_COMPARE_AND_SWAP_8
        return __sync_val_compare_and_swap(object, expected, desired);
#endif
    }
    static inline bool bool_cmpxchg(type* object, type expected, type desired) {
#if HAVE___SYNC_BOOL_COMPARE_AND_SWAP_8 && ALLOW___SYNC_BUILTINS
        return __sync_bool_compare_and_swap(object, expected, desired);
#elif __x86_64__
        bool result;
        asm volatile("lock; cmpxchgq %3,%1; sete %b2"
                     : "+a" (expected), "+m" (*object), "=q" (result)
                     : "r" (desired) : "cc");
        B()();
        return result;
#else
        uint32_t expected_low(expected), expected_high(expected >> 32),
            desired_low(desired), desired_high(desired >> 32);
        bool result;
        asm volatile("lock; cmpxchg8b %2; sete %b4"
                     : "+a" (expected_low), "+d" (expected_high),
                       "+m" (*object), "=q" (result)
                     : "b" (desired_low), "c" (desired_high) : "cc");
        B()();
        return result;
#endif
    }
#if __x86_64__ || HAVE___SYNC_FETCH_AND_ADD_8
    static inline type fetch_and_add(type* object, type addend) {
# if __x86_64__ && (PREFER_X86 || !HAVE___SYNC_FETCH_AND_ADD_8)
        asm volatile("lock; xaddq %0,%1"
                     : "+r" (addend), "+m" (*object) : : "cc");
        B()();
        return addend;
# else
        return __sync_fetch_and_add(object, addend);
# endif
    }
#endif
#if __x86_64__ || HAVE___SYNC_FETCH_AND_OR_8
    static inline void atomic_or(type* object, type addend) {
#if __x86_64__
        asm volatile("lock; orq %0,%1"
                     : "=r" (addend), "+m" (*object) : : "cc");
        B()();
#else
        __sync_fetch_and_or(object, addend);
#endif
    }
#endif
};

template<typename T>
inline T xchg(T* object, T new_value) {
    typedef sized_compiler_operations<sizeof(T), fence_function> sco_t;
    typedef typename sco_t::type type;
    return (T) sco_t::xchg((type*) object, (type) new_value);
}

inline int8_t xchg(int8_t* object, int new_value) {
    return xchg(object, (int8_t) new_value);
}
inline uint8_t xchg(uint8_t* object, int new_value) {
    return xchg(object, (uint8_t) new_value);
}
inline int16_t xchg(int16_t* object, int new_value) {
    return xchg(object, (int16_t) new_value);
}
inline uint16_t xchg(uint16_t* object, int new_value) {
    return xchg(object, (uint16_t) new_value);
}
inline unsigned xchg(unsigned* object, int new_value) {
    return xchg(object, (unsigned) new_value);
}

/** @brief Atomic compare and exchange. Return actual old value.
 * @param object pointer to memory value
 * @param expected old value
 * @param desired new value
 * @return actual old value
 *
 * Acts like an atomic version of:
 * @code
 * T actual(*object);
 * if (actual == expected)
 *    *object = desired;
 * return actual;
 * @endcode */
template <typename T>
inline T cmpxchg(T* object, T expected, T desired) {
    typedef sized_compiler_operations<sizeof(T), fence_function> sco_t;
    typedef typename sco_t::type type;
    return (T) sco_t::val_cmpxchg((type*) object, (type) expected, (type) desired);
}

inline unsigned cmpxchg(unsigned *object, int expected, int desired) {
    return cmpxchg(object, unsigned(expected), unsigned(desired));
}

/** @brief Atomic compare and exchange. Return true iff swap succeeds.
 * @param object pointer to memory value
 * @param expected old value
 * @param desired new value
 * @return true if swap succeeded, false otherwise
 *
 * Acts like an atomic version of:
 * @code
 * T actual(*object);
 * if (actual == expected) {
 *    *object = desired;
 *    return true;
 * } else
 *    return false;
 * @endcode */
template <typename T>
inline bool bool_cmpxchg(T* object, T expected, T desired) {
    typedef sized_compiler_operations<sizeof(T), fence_function> sco_t;
    typedef typename sco_t::type type;
    return sco_t::bool_cmpxchg((type*) object, (type) expected, (type) desired);
}

inline bool bool_cmpxchg(uint8_t* object, int expected, int desired) {
    return bool_cmpxchg(object, uint8_t(expected), uint8_t(desired));
}
inline bool bool_cmpxchg(unsigned *object, int expected, int desired) {
    return bool_cmpxchg(object, unsigned(expected), unsigned(desired));
}

/** @brief Atomic fetch-and-add. Return the old value.
 * @param object pointer to integer
 * @param addend value to add
 * @return old value */
template <typename T>
inline T fetch_and_add(T* object, T addend) {
    typedef sized_compiler_operations<sizeof(T), fence_function> sco_t;
    typedef typename sco_t::type type;
    return (T) sco_t::fetch_and_add((type*) object, (type) addend);
}

template <typename T>
inline T* fetch_and_add(T** object, int addend) {
    typedef sized_compiler_operations<sizeof(T*), fence_function> sco_t;
    typedef typename sco_t::type type;
    return (T*) sco_t::fetch_and_add((type*) object, (type) (addend * sizeof(T)));
}

inline char fetch_and_add(char* object, int addend) {
    return fetch_and_add(object, (char) addend);
}
inline signed char fetch_and_add(signed char* object, int addend) {
    return fetch_and_add(object, (signed char) addend);
}
inline unsigned char fetch_and_add(unsigned char* object, int addend) {
    return fetch_and_add(object, (unsigned char) addend);
}
inline short fetch_and_add(short* object, int addend) {
    return fetch_and_add(object, (short) addend);
}
inline unsigned short fetch_and_add(unsigned short* object, int addend) {
    return fetch_and_add(object, (unsigned short) addend);
}
inline unsigned fetch_and_add(unsigned* object, int addend) {
    return fetch_and_add(object, (unsigned) addend);
}
inline long fetch_and_add(long* object, int addend) {
    return fetch_and_add(object, (long) addend);
}
inline unsigned long fetch_and_add(unsigned long* object, int addend) {
    return fetch_and_add(object, (unsigned long) addend);
}
#if SIZEOF_LONG_LONG <= 8
inline long long fetch_and_add(long long* object, int addend) {
    return fetch_and_add(object, (long long) addend);
}
inline unsigned long long fetch_and_add(unsigned long long* object, int addend) {
    return fetch_and_add(object, (unsigned long long) addend);
}
#endif


/** @brief Test-and-set lock acquire. */
template <typename T>
inline void test_and_set_acquire(T* object) {
    typedef sized_compiler_operations<sizeof(T), do_nothing> sco_t;
    typedef typename sco_t::type type;
    while (sco_t::xchg((type*) object, (type) 1))
        relax_fence();
    acquire_fence();
}

/** @brief Test-and-set lock release. */
template <typename T>
inline void test_and_set_release(T* object) {
    release_fence();
    *object = T();
}


/** @brief Atomic fetch-and-or. Returns nothing.
 * @param object pointer to integer
 * @param addend value to or */
template <typename T>
inline void atomic_or(T* object, T addend) {
    typedef sized_compiler_operations<sizeof(T), fence_function> sco_t;
    typedef typename sco_t::type type;
    sco_t::atomic_or((type*) object, (type) addend);
}

inline void atomic_or(int8_t* object, int addend) {
    atomic_or(object, int8_t(addend));
}
inline void atomic_or(uint8_t* object, int addend) {
    atomic_or(object, uint8_t(addend));
}
inline void atomic_or(int16_t* object, int addend) {
    atomic_or(object, int16_t(addend));
}
inline void atomic_or(uint16_t* object, int addend) {
    atomic_or(object, uint16_t(addend));
}
inline void atomic_or(unsigned* object, int addend) {
    atomic_or(object, unsigned(addend));
}
inline void atomic_or(unsigned long* object, int addend) {
    atomic_or(object, (unsigned long)(addend));
}


// prefetch instruction
#if !PREFETCH_DEFINED
inline void prefetch(const void *ptr) {
#ifdef NOPREFETCH
    (void) ptr;
#else
    typedef struct { char x[CACHE_LINE_SIZE]; } cacheline_t;
    asm volatile("prefetcht0 %0" : : "m" (*(const cacheline_t *)ptr));
#endif
}
#endif

inline void prefetchnta(const void *ptr) {
#ifdef NOPREFETCH
    (void) ptr;
#else
    typedef struct { char x[CACHE_LINE_SIZE]; } cacheline_t;
    asm volatile("prefetchnta %0" : : "m" (*(const cacheline_t *)ptr));
#endif
}


template <typename T>
struct value_prefetcher {
    void operator()(T) {
    }
};

template <typename T>
struct value_prefetcher<T *> {
    void operator()(T *p) {
        prefetch((const void *) p);
    }
};


// stolen from Linux
inline uint64_t ntohq(uint64_t val) {
#ifdef __i386__
    union {
        struct {
            uint32_t a;
            uint32_t b;
        } s;
        uint64_t u;
    } v;
    v.u = val;
    asm("bswapl %0; bswapl %1; xchgl %0,%1"
        : "+r" (v.s.a), "+r" (v.s.b));
    return v.u;
#else /* __i386__ */
    asm("bswapq %0" : "+r" (val));
    return val;
#endif
}

inline uint64_t htonq(uint64_t val) {
    return ntohq(val);
}


/** Bit counting. */

/** @brief Return the number of leading 0 bits in @a x.
 * @pre @a x != 0
 *
 * "Leading" means "most significant." */
#if HAVE___BUILTIN_CLZ
inline int clz(int x) {
    return __builtin_clz(x);
}
inline int clz(unsigned x) {
    return __builtin_clz(x);
}
#endif

#if HAVE___BUILTIN_CLZL
inline int clz(long x) {
    return __builtin_clzl(x);
}
inline int clz(unsigned long x) {
    return __builtin_clzl(x);
}
#endif

#if HAVE___BUILTIN_CLZLL
inline int clz(long long x) {
    return __builtin_clzll(x);
}
inline int clz(unsigned long long x) {
    return __builtin_clzll(x);
}
#endif

/** @brief Return the number of trailing 0 bits in @a x.
 * @pre @a x != 0
 *
 * "Trailing" means "least significant." */
#if HAVE___BUILTIN_CTZ
inline int ctz(int x) {
    return __builtin_ctz(x);
}
inline int ctz(unsigned x) {
    return __builtin_ctz(x);
}
#endif

#if HAVE___BUILTIN_CTZL
inline int ctz(long x) {
    return __builtin_ctzl(x);
}
inline int ctz(unsigned long x) {
    return __builtin_ctzl(x);
}
#endif

#if HAVE___BUILTIN_CTZLL
inline int ctz(long long x) {
    return __builtin_ctzll(x);
}
inline int ctz(unsigned long long x) {
    return __builtin_ctzll(x);
}
#endif

template <typename T, typename U>
inline T iceil(T x, U y) {
    U mod = x % y;
    return x + (mod ? y - mod : 0);
}

/** @brief Return the smallest power of 2 greater than or equal to @a x.
    @pre @a x != 0
    @pre the result is representable in type T (that is, @a x can't be
    larger than the largest power of 2 representable in type T) */
template <typename T>
inline T iceil_log2(T x) {
    return T(1) << (sizeof(T) * 8 - clz(x) - !(x & (x - 1)));
}

/** @brief Return the largest power of 2 less than or equal to @a x.
    @pre @a x != 0 */
template <typename T>
inline T ifloor_log2(T x) {
    return T(1) << (sizeof(T) * 8 - 1 - clz(x));
}

/** @brief Return the index of the lowest 0 nibble in @a x.
 *
 * 0 is the lowest-order nibble. Returns -1 if no nibbles are 0. */
template <typename T>
inline int find_lowest_zero_nibble(T x) {
    static_assert(sizeof(T) <= sizeof(unsigned long long), "T is too big");
#if SIZEOF_LONG_LONG == 16
    T h = T(0x88888888888888888888888888888888ULL), l = T(0x11111111111111111111111111111111ULL);
#else
    T h = T(0x8888888888888888ULL), l = T(0x1111111111111111ULL);
#endif
    T t = h & (x - l) & ~x;
    return t ? ctz(t) >> 2 : -1;
}

/** @brief Translate @a x to network byte order.
 *
 * Compare htons/htonl/htonq.  host_to_net_order is particularly useful in
 * template functions, where the type to be translated to network byte order
 * is unknown. */
inline unsigned char host_to_net_order(unsigned char x) {
    return x;
}
/** @overload */
inline signed char host_to_net_order(signed char x) {
    return x;
}
/** @overload */
inline char host_to_net_order(char x) {
    return x;
}
/** @overload */
inline short host_to_net_order(short x) {
    return htons(x);
}
/** @overload */
inline unsigned short host_to_net_order(unsigned short x) {
    return htons(x);
}
/** @overload */
inline int host_to_net_order(int x) {
    return htonl(x);
}
/** @overload */
inline unsigned host_to_net_order(unsigned x) {
    return htonl(x);
}
#if SIZEOF_LONG == 4
/** @overload */
inline long host_to_net_order(long x) {
    return htonl(x);
}
/** @overload */
inline unsigned long host_to_net_order(unsigned long x) {
    return htonl(x);
}
#elif SIZEOF_LONG == 8
/** @overload */
inline long host_to_net_order(long x) {
    return htonq(x);
}
/** @overload */
inline unsigned long host_to_net_order(unsigned long x) {
    return htonq(x);
}
#endif
#if SIZEOF_LONG_LONG == 8
/** @overload */
inline long long host_to_net_order(long long x) {
    return htonq(x);
}
/** @overload */
inline unsigned long long host_to_net_order(unsigned long long x) {
    return htonq(x);
}
#endif
#if !HAVE_INT64_T_IS_LONG && !HAVE_INT64_T_IS_LONG_LONG
/** @overload */
inline int64_t host_to_net_order(int64_t x) {
    return htonq(x);
}
/** @overload */
inline uint64_t host_to_net_order(uint64_t x) {
    return htonq(x);
}
#endif
/** @overload */
inline double host_to_net_order(float x) {
    union { float f; uint32_t i; } v;
    v.f = x;
    v.i = host_to_net_order(v.i);
    return v.f;
}
/** @overload */
inline double host_to_net_order(double x) {
    union { double d; uint64_t i; } v;
    v.d = x;
    v.i = host_to_net_order(v.i);
    return v.d;
}

/** @brief Translate @a x to host byte order.
 *
 * Compare ntohs/ntohl/ntohq.  net_to_host_order is particularly useful in
 * template functions, where the type to be translated to network byte order
 * is unknown. */
inline unsigned char net_to_host_order(unsigned char x) {
    return x;
}
/** @overload */
inline signed char net_to_host_order(signed char x) {
    return x;
}
/** @overload */
inline char net_to_host_order(char x) {
    return x;
}
/** @overload */
inline short net_to_host_order(short x) {
    return ntohs(x);
}
/** @overload */
inline unsigned short net_to_host_order(unsigned short x) {
    return ntohs(x);
}
/** @overload */
inline int net_to_host_order(int x) {
    return ntohl(x);
}
/** @overload */
inline unsigned net_to_host_order(unsigned x) {
    return ntohl(x);
}
#if SIZEOF_LONG == 4
/** @overload */
inline long net_to_host_order(long x) {
    return ntohl(x);
}
/** @overload */
inline unsigned long net_to_host_order(unsigned long x) {
    return ntohl(x);
}
#elif SIZEOF_LONG == 8
/** @overload */
inline long net_to_host_order(long x) {
    return ntohq(x);
}
/** @overload */
inline unsigned long net_to_host_order(unsigned long x) {
    return ntohq(x);
}
#endif
#if SIZEOF_LONG_LONG == 8
/** @overload */
inline long long net_to_host_order(long long x) {
    return ntohq(x);
}
/** @overload */
inline unsigned long long net_to_host_order(unsigned long long x) {
    return ntohq(x);
}
#endif
#if !HAVE_INT64_T_IS_LONG && !HAVE_INT64_T_IS_LONG_LONG
/** @overload */
inline int64_t net_to_host_order(int64_t x) {
    return ntohq(x);
}
/** @overload */
inline uint64_t net_to_host_order(uint64_t x) {
    return ntohq(x);
}
#endif
/** @overload */
inline double net_to_host_order(float x) {
    return host_to_net_order(x);
}
/** @overload */
inline double net_to_host_order(double x) {
    return host_to_net_order(x);
}

template <typename T> struct make_aliasable {};
#define MAKE_ALIASABLE(T) template <> struct make_aliasable<T> { typedef T type __attribute__((__may_alias__)); }
MAKE_ALIASABLE(unsigned char);
MAKE_ALIASABLE(signed char);
MAKE_ALIASABLE(char);
MAKE_ALIASABLE(unsigned short);
MAKE_ALIASABLE(short);
MAKE_ALIASABLE(int);
MAKE_ALIASABLE(unsigned);
MAKE_ALIASABLE(long);
MAKE_ALIASABLE(unsigned long);
MAKE_ALIASABLE(long long);
MAKE_ALIASABLE(unsigned long long);
MAKE_ALIASABLE(float);
MAKE_ALIASABLE(double);
#undef MAKE_ALIASABLE

template <typename T>
inline char* write_in_host_order(char* s, T x) {
#if HAVE_INDIFFERENT_ALIGNMENT
    *reinterpret_cast<typename make_aliasable<T>::type*>(s) = x;
#else
    memcpy(s, &x, sizeof(x));
#endif
    return s + sizeof(x);
}

template <typename T>
inline uint8_t* write_in_host_order(uint8_t* s, T x) {
    return reinterpret_cast<uint8_t*>
        (write_in_host_order(reinterpret_cast<char*>(s), x));
}

template <typename T>
inline T read_in_host_order(const char* s) {
#if HAVE_INDIFFERENT_ALIGNMENT
    return *reinterpret_cast<const typename make_aliasable<T>::type*>(s);
#else
    T x;
    memcpy(&x, s, sizeof(x));
    return x;
#endif
}

template <typename T>
inline T read_in_host_order(const uint8_t* s) {
    return read_in_host_order<T>(reinterpret_cast<const char*>(s));
}

template <typename T>
inline char* write_in_net_order(char* s, T x) {
    return write_in_host_order<T>(s, host_to_net_order(x));
}

template <typename T>
inline uint8_t* write_in_net_order(uint8_t* s, T x) {
    return reinterpret_cast<uint8_t*>
        (write_in_net_order(reinterpret_cast<char*>(s), x));
}

template <typename T>
inline T read_in_net_order(const char* s) {
    return net_to_host_order(read_in_host_order<T>(s));
}

template <typename T>
inline T read_in_net_order(const uint8_t* s) {
    return read_in_net_order<T>(reinterpret_cast<const char*>(s));
}


inline uint64_t read_pmc(uint32_t ecx) {
    uint32_t a, d;
    __asm __volatile("rdpmc" : "=a"(a), "=d"(d) : "c"(ecx));
    return ((uint64_t)a) | (((uint64_t)d) << 32);
}

inline uint64_t read_tsc(void)
{
    uint32_t low, high;
    asm volatile("rdtsc" : "=a" (low), "=d" (high));
    return ((uint64_t)low) | (((uint64_t)high) << 32);
}

template <typename T>
inline int compare(T a, T b) {
    if (a == b)
        return 0;
    else
        return a < b ? -1 : 1;
}


/** Type traits **/
namespace mass {

template <typename T> struct type_synonym {
    typedef T type;
};


#if HAVE_CXX_TEMPLATE_ALIAS && HAVE_TYPE_TRAITS
template <typename T, T V>
using integral_constant = std::integral_constant<T, V>;
typedef std::true_type true_type;
typedef std::false_type false_type;
#else
template <typename T, T V>
struct integral_constant {
    typedef integral_constant<T, V> type;
    typedef T value_type;
    static constexpr T value = V;
};
template <typename T, T V> constexpr T integral_constant<T, V>::value;
typedef integral_constant<bool, true> true_type;
typedef integral_constant<bool, false> false_type;
#endif

#if HAVE_CXX_TEMPLATE_ALIAS && HAVE_TYPE_TRAITS
template <bool B, typename T, typename F>
using conditional = std::conditional<B, T, F>;
#else
template <bool B, typename T, typename F> struct conditional {};
template <typename T, typename F> struct conditional<true, T, F> {
    typedef T type;
};
template <typename T, typename F> struct conditional<false, T, F> {
    typedef F type;
};
#endif

#if HAVE_CXX_TEMPLATE_ALIAS && HAVE_TYPE_TRAITS
template <typename T> using remove_const = std::remove_const<T>;
template <typename T> using remove_volatile = std::remove_volatile<T>;
template <typename T> using remove_cv = std::remove_cv<T>;
#else
template <typename T> struct remove_const : public type_synonym<T> {};
template <typename T> struct remove_const<const T> : public type_synonym<T> {};
template <typename T> struct remove_volatile : public type_synonym<T> {};
template <typename T> struct remove_volatile<volatile T> : public type_synonym<T> {};
template <typename T> struct remove_cv {
    typedef typename remove_const<typename remove_volatile<T>::type>::type type;
};
#endif

#if HAVE_CXX_TEMPLATE_ALIAS && HAVE_TYPE_TRAITS
template <typename T> using is_pointer = std::is_pointer<T>;
#else
template <typename T> struct is_pointer_helper : public false_type {};
template <typename T> struct is_pointer_helper<T*> : public true_type {};
template <typename T> struct is_pointer
    : public integral_constant<bool, is_pointer_helper<typename remove_cv<T>::type>::value> {};
#endif

#if HAVE_CXX_TEMPLATE_ALIAS && HAVE_TYPE_TRAITS
template <typename T> using is_reference = std::is_reference<T>;
#else
template <typename T> struct is_reference_helper : public false_type {};
template <typename T> struct is_reference_helper<T&> : public true_type {};
template <typename T> struct is_reference_helper<T&&> : public true_type {};
template <typename T> struct is_reference
    : public integral_constant<bool, is_reference_helper<typename remove_cv<T>::type>::value> {};
#endif

#if HAVE_CXX_TEMPLATE_ALIAS && HAVE_TYPE_TRAITS
template <typename T> using make_unsigned = std::make_unsigned<T>;
template <typename T> using make_signed = std::make_signed<T>;
#else
template <typename T> struct make_unsigned {};
template <> struct make_unsigned<char> : public type_synonym<unsigned char> {};
template <> struct make_unsigned<signed char> : public type_synonym<unsigned char> {};
template <> struct make_unsigned<unsigned char> : public type_synonym<unsigned char> {};
template <> struct make_unsigned<short> : public type_synonym<unsigned short> {};
template <> struct make_unsigned<unsigned short> : public type_synonym<unsigned short> {};
template <> struct make_unsigned<int> : public type_synonym<unsigned> {};
template <> struct make_unsigned<unsigned> : public type_synonym<unsigned> {};
template <> struct make_unsigned<long> : public type_synonym<unsigned long> {};
template <> struct make_unsigned<unsigned long> : public type_synonym<unsigned long> {};
template <> struct make_unsigned<long long> : public type_synonym<unsigned long long> {};
template <> struct make_unsigned<unsigned long long> : public type_synonym<unsigned long long> {};

template <typename T> struct make_signed {};
template <> struct make_signed<char> : public type_synonym<signed char> {};
template <> struct make_signed<signed char> : public type_synonym<signed char> {};
template <> struct make_signed<unsigned char> : public type_synonym<signed char> {};
template <> struct make_signed<short> : public type_synonym<short> {};
template <> struct make_signed<unsigned short> : public type_synonym<short> {};
template <> struct make_signed<int> : public type_synonym<int> {};
template <> struct make_signed<unsigned> : public type_synonym<int> {};
template <> struct make_signed<long> : public type_synonym<long> {};
template <> struct make_signed<unsigned long> : public type_synonym<long> {};
template <> struct make_signed<long long> : public type_synonym<long long> {};
template <> struct make_signed<unsigned long long> : public type_synonym<long long> {};
#endif


/** @class is_trivially_copyable
  @brief Template determining whether T may be copied by memcpy.

  is_trivially_copyable<T> is equivalent to true_type if T has a trivial
  copy constructor, false_type if it does not. */

#if HAVE_CXX_TEMPLATE_ALIAS && HAVE_TYPE_TRAITS && HAVE_STD_IS_TRIVIALLY_COPYABLE
template <typename T> using is_trivially_copyable = std::is_trivially_copyable<T>;
#elif HAVE___HAS_TRIVIAL_COPY
template <typename T> struct is_trivially_copyable : public integral_constant<bool, __has_trivial_copy(T)> {};
#else
template <typename T> struct is_trivially_copyable : public false_type {};
template <> struct is_trivially_copyable<unsigned char> : public true_type {};
template <> struct is_trivially_copyable<signed char> : public true_type {};
template <> struct is_trivially_copyable<char> : public true_type {};
template <> struct is_trivially_copyable<unsigned short> : public true_type {};
template <> struct is_trivially_copyable<short> : public true_type {};
template <> struct is_trivially_copyable<unsigned> : public true_type {};
template <> struct is_trivially_copyable<int> : public true_type {};
template <> struct is_trivially_copyable<unsigned long> : public true_type {};
template <> struct is_trivially_copyable<long> : public true_type {};
template <> struct is_trivially_copyable<unsigned long long> : public true_type {};
template <> struct is_trivially_copyable<long long> : public true_type {};
template <typename T> struct is_trivially_copyable<T*> : public true_type {};
#endif


/** @class is_trivially_destructible
  @brief Template determining whether T may be trivially destructed. */

#if HAVE_CXX_TEMPLATE_ALIAS && HAVE_TYPE_TRAITS && HAVE_STD_IS_TRIVIALLY_DESTRUCTIBLE
template <typename T> using is_trivially_destructible = std::is_trivially_destructible<T>;
#elif HAVE___HAS_TRIVIAL_DESTRUCTOR
template <typename T> struct is_trivially_destructible : public integral_constant<bool, __has_trivial_destructor(T)> {};
#else
template <typename T> struct is_trivially_destructible : public is_trivially_copyable<T> {};
#endif


/** @class fast_argument
  @brief Template defining a fast argument type for objects of type T.

  fast_argument<T>::type equals either "const T&" or "T".
  fast_argument<T>::is_reference is true iff fast_argument<T>::type is
  a reference. If fast_argument<T>::is_reference is true, then
  fast_argument<T>::enable_rvalue_reference is a typedef to void; otherwise
  it is not defined. */
template <typename T, bool use_reference = (!is_reference<T>::value
                                            && (!is_trivially_copyable<T>::value
                                                || sizeof(T) > sizeof(void *)))>
struct fast_argument;

template <typename T> struct fast_argument<T, true> {
    static constexpr bool is_reference = true;
    typedef const T& type;
    typedef void enable_rvalue_reference;
};
template <typename T> struct fast_argument<T, false> {
    static constexpr bool is_reference = false;
    typedef T type;
};
template <typename T> constexpr bool fast_argument<T, true>::is_reference;
template <typename T> constexpr bool fast_argument<T, false>::is_reference;

}


template <typename T>
struct has_fast_int_multiply : public mass::false_type {
    // enum { check_t_integral = mass::integer_traits<T>::is_signed };
};

#if defined(__i386__) || defined(__x86_64__)
inline void int_multiply(unsigned a, unsigned b, unsigned &xlow, unsigned &xhigh)
{
    __asm__("mul %2" : "=a" (xlow), "=d" (xhigh) : "r" (a), "a" (b) : "cc");
}
template <> struct has_fast_int_multiply<unsigned> : public mass::true_type {};

# if SIZEOF_LONG == 4 || (defined(__x86_64__) && SIZEOF_LONG == 8)
inline void int_multiply(unsigned long a, unsigned long b, unsigned long &xlow, unsigned long &xhigh)
{
    __asm__("mul %2" : "=a" (xlow), "=d" (xhigh) : "r" (a), "a" (b) : "cc");
}
template <> struct has_fast_int_multiply<unsigned long> : public mass::true_type {};
# endif

# if defined(__x86_64__) && SIZEOF_LONG_LONG == 8
inline void int_multiply(unsigned long long a, unsigned long long b, unsigned long long &xlow, unsigned long long &xhigh)
{
    __asm__("mul %2" : "=a" (xlow), "=d" (xhigh) : "r" (a), "a" (b) : "cc");
}
template <> struct has_fast_int_multiply<unsigned long long> : public mass::true_type {};
# endif
#endif

struct uninitialized_type {};

#endif

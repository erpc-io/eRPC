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
#ifndef KSEARCH_HH
#define KSEARCH_HH 1
#include "kpermuter.hh"

template <typename KA, typename T>
struct key_comparator {
    int operator()(const KA& ka, const T& n, int p) {
        return n.compare_key(ka, p);
    }
};

struct key_indexed_position {
    int i;
    int p;
    inline key_indexed_position() {
    }
    inline constexpr key_indexed_position(int i_, int p_)
        : i(i_), p(p_) {
    }
};


template <typename KA, typename T, typename F>
int key_upper_bound_by(const KA& ka, const T& n, F comparator)
{
    typename key_permuter<T>::type perm = key_permuter<T>::permutation(n);
    int l = 0, r = perm.size();
    while (l < r) {
        int m = (l + r) >> 1;
        int mp = perm[m];
        int cmp = comparator(ka, n, mp);
        if (cmp < 0)
            r = m;
        else if (cmp == 0)
            return m + 1;
        else
            l = m + 1;
    }
    return l;
}

template <typename KA, typename T>
inline int key_upper_bound(const KA& ka, const T& n)
{
    return key_upper_bound_by(ka, n, key_comparator<KA, T>());
}

template <typename KA, typename T, typename F>
key_indexed_position key_lower_bound_by(const KA& ka, const T& n, F comparator)
{
    typename key_permuter<T>::type perm = key_permuter<T>::permutation(n);
    int l = 0, r = perm.size();
    while (l < r) {
        int m = (l + r) >> 1;
        int mp = perm[m];
        int cmp = comparator(ka, n, mp);
        if (cmp < 0)
            r = m;
        else if (cmp == 0)
            return key_indexed_position(m, mp);
        else
            l = m + 1;
    }
    return key_indexed_position(l, -1);
}

template <typename KA, typename T>
inline key_indexed_position key_lower_bound(const KA& ka, const T& n)
{
    return key_lower_bound_by(ka, n, key_comparator<KA, T>());
}


template <typename KA, typename T, typename F>
int key_find_upper_bound_by(const KA& ka, const T& n, F comparator)
{
    typename key_permuter<T>::type perm = key_permuter<T>::permutation(n);
    int l = 0, r = perm.size();
    while (l < r) {
        int lp = perm[l];
        int cmp = comparator(ka, n, lp);
        if (cmp < 0)
            break;
        else
            ++l;
    }
    return l;
}

template <typename KA, typename T, typename F>
key_indexed_position key_find_lower_bound_by(const KA& ka, const T& n, F comparator)
{
    typename key_permuter<T>::type perm = key_permuter<T>::permutation(n);
    int l = 0, r = perm.size();
    while (l < r) {
        int lp = perm[l];
        int cmp = comparator(ka, n, lp);
        if (cmp < 0)
            break;
        else if (cmp == 0)
            return key_indexed_position(l, lp);
        else
            ++l;
    }
    return key_indexed_position(l, -1);
}


struct key_bound_binary {
    static constexpr bool is_binary = true;
    template <typename KA, typename T>
    static inline int upper(const KA& ka, const T& n) {
        return key_upper_bound_by(ka, n, key_comparator<KA, T>());
    }
    template <typename KA, typename T>
    static inline key_indexed_position lower(const KA& ka, const T& n) {
        return key_lower_bound_by(ka, n, key_comparator<KA, T>());
    }
    template <typename KA, typename T, typename F>
    static inline key_indexed_position lower_by(const KA& ka, const T& n, F comparator) {
        return key_lower_bound_by(ka, n, comparator);
    }
};

struct key_bound_linear {
    static constexpr bool is_binary = false;
    template <typename KA, typename T>
    static inline int upper(const KA& ka, const T& n) {
        return key_find_upper_bound_by(ka, n, key_comparator<KA, T>());
    }
    template <typename KA, typename T>
    static inline key_indexed_position lower(const KA& ka, const T& n) {
        return key_find_lower_bound_by(ka, n, key_comparator<KA, T>());
    }
    template <typename KA, typename T, typename F>
    static inline key_indexed_position lower_by(const KA& ka, const T& n, F comparator) {
        return key_find_lower_bound_by(ka, n, comparator);
    }
};


enum {
    bound_method_fast = 0,
    bound_method_binary,
    bound_method_linear
};
template <int max_size, int method = bound_method_fast> struct key_bound {};
template <int max_size> struct key_bound<max_size, bound_method_binary> {
    typedef key_bound_binary type;
};
template <int max_size> struct key_bound<max_size, bound_method_linear> {
    typedef key_bound_linear type;
};
template <int max_size> struct key_bound<max_size, bound_method_fast> {
    typedef typename key_bound<max_size, (max_size > 16 ? bound_method_binary : bound_method_linear)>::type type;
};

#endif

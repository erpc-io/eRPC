#ifndef GSTORE_SMALL_VECTOR_HH
#define GSTORE_SMALL_VECTOR_HH 1
#include "compiler.hh"
#include <memory>
#include <iterator>
#include <assert.h>

template <typename T, unsigned N, typename A = std::allocator<T> >
class small_vector {
  public:
    typedef bool (small_vector<T, N, A>::*unspecified_bool_type)() const;
    typedef T value_type;
    typedef value_type* iterator;
    typedef const value_type* const_iterator;
    typedef std::reverse_iterator<iterator> reverse_iterator;
    typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
    typedef unsigned size_type;
    static constexpr size_type small_capacity = N;

    inline small_vector(const A& allocator = A());
    small_vector(const small_vector<T, N, A>& x);
    template <unsigned NN, typename AA>
    small_vector(const small_vector<T, NN, AA>& x);
    inline ~small_vector();

    inline size_type size() const;
    inline size_type capacity() const;
    inline bool empty() const;
    inline operator unspecified_bool_type() const;
    inline bool operator!() const;

    inline iterator begin();
    inline iterator end();
    inline const_iterator begin() const;
    inline const_iterator end() const;
    inline const_iterator cbegin() const;
    inline const_iterator cend() const;
    inline reverse_iterator rbegin();
    inline reverse_iterator rend();
    inline const_reverse_iterator rbegin() const;
    inline const_reverse_iterator rend() const;
    inline const_reverse_iterator crbegin() const;
    inline const_reverse_iterator crend() const;

    inline value_type& operator[](size_type i);
    inline const value_type& operator[](size_type i) const;
    inline value_type& front();
    inline const value_type& front() const;
    inline value_type& back();
    inline const value_type& back() const;

    inline void push_back(const value_type& x);
    inline void push_back(value_type&& x);
    template <typename... Args> inline void emplace_back(Args&&... args);
    inline void pop_back();

    inline void clear();
    inline void resize(size_type n, value_type x = value_type());
    iterator erase(iterator position);
    iterator erase(iterator first, iterator last);

    inline small_vector<T, N, A>& operator=(const small_vector<T, N, A>& x);
    template <unsigned NN, typename AA>
    inline small_vector<T, N, A>& operator=(const small_vector<T, NN, AA>& x);

  private:
    struct rep : public A {
        T* first_;
        T* last_;
        T* capacity_;
        char lv_[sizeof(T) * N]; // XXX does not obey alignof(T)

        inline rep(const A& a);
    };
    rep r_;

    void grow(size_type n = 0);
};

template <typename T, unsigned N, typename A>
inline small_vector<T, N, A>::rep::rep(const A& a)
    : A(a), first_(reinterpret_cast<T*>(lv_)),
      last_(first_), capacity_(first_ + N) {
}

template <typename T, unsigned N, typename A>
inline small_vector<T, N, A>::small_vector(const A& allocator)
    : r_(allocator) {
}

template <typename T, unsigned N, typename A>
small_vector<T, N, A>::small_vector(const small_vector<T, N, A>& x)
    : r_(A()) {
    for (const T* it = x.r_.first_; it != x.r_.last_; ++it)
        push_back(*it);
}

template <typename T, unsigned N, typename A>
template <unsigned NN, typename AA>
small_vector<T, N, A>::small_vector(const small_vector<T, NN, AA>& x)
    : r_(A()) {
    for (const T* it = x.r_.first_; it != x.r_.last_; ++it)
        push_back(*it);
}

template <typename T, unsigned N, typename A>
inline small_vector<T, N, A>::~small_vector() {
    for (T* it = r_.first_; it != r_.last_; ++it)
        r_.destroy(it);
    if (r_.first_ != reinterpret_cast<T*>(r_.lv_))
        r_.deallocate(r_.first_, r_.capacity_ - r_.first_);
}

template <typename T, unsigned N, typename A>
inline unsigned small_vector<T, N, A>::size() const {
    return r_.last_ - r_.first_;
}

template <typename T, unsigned N, typename A>
inline unsigned small_vector<T, N, A>::capacity() const {
    return r_.capacity_ - r_.first_;
}

template <typename T, unsigned N, typename A>
inline bool small_vector<T, N, A>::empty() const {
    return r_.first_ == r_.last_;
}

template <typename T, unsigned N, typename A>
inline small_vector<T, N, A>::operator unspecified_bool_type() const {
    return empty() ? 0 : &small_vector<T, N, A>::empty;
}

template <typename T, unsigned N, typename A>
inline bool small_vector<T, N, A>::operator!() const {
    return empty();
}

template <typename T, unsigned N, typename A>
void small_vector<T, N, A>::grow(size_type n) {
    size_t newcap = capacity() * 2;
    while (newcap < n)
        newcap *= 2;
    T* m = r_.allocate(newcap);
    for (T* it = r_.first_, *mit = m; it != r_.last_; ++it, ++mit) {
        r_.construct(mit, std::move(*it));
        r_.destroy(it);
    }
    if (r_.first_ != reinterpret_cast<T*>(r_.lv_))
        r_.deallocate(r_.first_, capacity());
    r_.last_ = m + (r_.last_ - r_.first_);
    r_.first_ = m;
    r_.capacity_ = m + newcap;
}

template <typename T, unsigned N, typename A>
inline auto small_vector<T, N, A>::begin() -> iterator {
    return r_.first_;
}

template <typename T, unsigned N, typename A>
inline auto small_vector<T, N, A>::end() -> iterator {
    return r_.last_;
}

template <typename T, unsigned N, typename A>
inline auto small_vector<T, N, A>::begin() const -> const_iterator {
    return r_.first_;
}

template <typename T, unsigned N, typename A>
inline auto small_vector<T, N, A>::end() const -> const_iterator {
    return r_.last_;
}

template <typename T, unsigned N, typename A>
inline auto small_vector<T, N, A>::cbegin() const -> const_iterator {
    return r_.first_;
}

template <typename T, unsigned N, typename A>
inline auto small_vector<T, N, A>::cend() const -> const_iterator {
    return r_.last_;
}

template <typename T, unsigned N, typename A>
inline auto small_vector<T, N, A>::rbegin() -> reverse_iterator {
    return reverse_iterator(end());
}

template <typename T, unsigned N, typename A>
inline auto small_vector<T, N, A>::rend() -> reverse_iterator {
    return reverse_iterator(begin());
}

template <typename T, unsigned N, typename A>
inline auto small_vector<T, N, A>::rbegin() const -> const_reverse_iterator {
    return const_reverse_iterator(end());
}

template <typename T, unsigned N, typename A>
inline auto small_vector<T, N, A>::rend() const -> const_reverse_iterator {
    return const_reverse_iterator(begin());
}

template <typename T, unsigned N, typename A>
inline auto small_vector<T, N, A>::crbegin() const -> const_reverse_iterator {
    return const_reverse_iterator(end());
}

template <typename T, unsigned N, typename A>
inline auto small_vector<T, N, A>::crend() const -> const_reverse_iterator {
    return const_reverse_iterator(begin());
}

template <typename T, unsigned N, typename A>
inline T& small_vector<T, N, A>::operator[](size_type i) {
    return r_.first_[i];
}

template <typename T, unsigned N, typename A>
inline const T& small_vector<T, N, A>::operator[](size_type i) const {
    return r_.first_[i];
}

template <typename T, unsigned N, typename A>
inline T& small_vector<T, N, A>::front() {
    return r_.first_[0];
}

template <typename T, unsigned N, typename A>
inline const T& small_vector<T, N, A>::front() const {
    return r_.first_[0];
}

template <typename T, unsigned N, typename A>
inline T& small_vector<T, N, A>::back() {
    return r_.last_[-1];
}

template <typename T, unsigned N, typename A>
inline const T& small_vector<T, N, A>::back() const {
    return r_.last_[-1];
}

template <typename T, unsigned N, typename A>
inline void small_vector<T, N, A>::push_back(const T& x) {
    if (r_.last_ == r_.capacity_)
        grow();
    r_.construct(r_.last_, x);
    ++r_.last_;
}

template <typename T, unsigned N, typename A>
inline void small_vector<T, N, A>::push_back(T&& x) {
    if (r_.last_ == r_.capacity_)
        grow();
    r_.construct(r_.last_, std::move(x));
    ++r_.last_;
}

template <typename T, unsigned N, typename A> template <typename... Args>
inline void small_vector<T, N, A>::emplace_back(Args&&... args) {
    if (r_.last_ == r_.capacity_)
        grow();
    r_.construct(r_.last_, std::forward<Args>(args)...);
    ++r_.last_;
}

template <typename T, unsigned N, typename A>
inline void small_vector<T, N, A>::pop_back() {
    assert(r_.first_ != r_.last_);
    --r_.last_;
    r_.destroy(r_.last_);
}

template <typename T, unsigned N, typename A>
inline void small_vector<T, N, A>::clear() {
    for (auto it = r_.first_; it != r_.last_; ++it)
        r_.destroy(it);
    r_.last_ = r_.first_;
}

template <typename T, unsigned N, typename A>
inline void small_vector<T, N, A>::resize(size_type n, value_type v) {
    if (capacity() < n)
        grow(n);
    auto it = r_.first_ + n;
    auto xt = r_.last_;
    r_.last_ = it;
    for (; it < xt; ++it)
        r_.destroy(it);
    for (; xt < it; ++xt)
        r_.construct(xt, v);
}

template <typename T, unsigned N, typename A>
small_vector<T, N, A>&
small_vector<T, N, A>::operator=(const small_vector<T, N, A>& x) {
    if (&x != this) {
        clear();
        if (capacity() < x.capacity())
            grow(x.capacity());
        for (auto xit = x.r_.first_; xit != x.r_.last_; ++xit, ++r_.last_)
            r_.construct(r_.last_, *xit);
    }
    return *this;
}

template <typename T, unsigned N, typename A>
template <unsigned NN, typename AA>
small_vector<T, N, A>&
small_vector<T, N, A>::operator=(const small_vector<T, NN, AA>& x) {
    clear();
    if (capacity() < x.capacity())
        grow(x.capacity());
    for (auto xit = x.r_.first_; xit != x.r_.last_; ++xit, ++r_.last_)
        r_.construct(r_.last_, *xit);
    return *this;
}

template <typename T, unsigned N, typename A>
inline T* small_vector<T, N, A>::erase(iterator position) {
    return erase(position, position + 1);
}

template <typename T, unsigned N, typename A>
T* small_vector<T, N, A>::erase(iterator first, iterator last) {
    if (first != last) {
        iterator it = first, xend = end();
        for (; last != xend; ++it, ++last)
            *it = std::move(*last);
        r_.last_ = it;
        for (; it != xend; ++it)
            r_.destroy(it);
    }
    return first;
}

template <typename T, unsigned N, typename A>
constexpr typename small_vector<T, N, A>::size_type
  small_vector<T, N, A>::small_capacity;

#endif

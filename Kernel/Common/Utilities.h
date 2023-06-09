#pragma once

#include "Core/Runtime.h"
#include "Memory.h"
#include "Traits.h"

#ifdef TEST_ENVIRONMENT
#include <algorithm>
#include <utility>
using std::forward;
using std::max;
using std::min;
using std::move;
using std::swap;
#endif

namespace kernel {

#ifndef TEST_ENVIRONMENT
template <typename T>
T&& forward(remove_reference_t<T>& value)
{
    return static_cast<T&&>(value);
}

template <typename T>
T&& forward(remove_reference_t<T>&& value)
{
    // TODO: assert that we're not forwarding an rvalue as an lvalue here
    return static_cast<T&&>(value);
}

template <typename T>
remove_reference_t<T>&& move(T&& value)
{
    return static_cast<remove_reference_t<T>&&>(value);
}

template <typename T>
constexpr const T& max(const T& l, const T& r)
{
    return l < r ? r : l;
}

template <typename T>
constexpr const T& min(const T& l, const T& r)
{
    return l < r ? l : r;
}

template <typename T>
void swap(T& l, T& r)
{
    T tmp = move(l);
    l = move(r);
    r = move(tmp);
}
#endif

// clang-format off
template <typename To, typename From>
enable_if_t<
    sizeof(To) == sizeof(From) &&
    is_trivially_constructible_v<To> &&
    is_trivially_copyable_v<To> &&
    is_trivially_copyable_v<From>,
    To
>
bit_cast(const From& value)
{
    To to;
    copy_memory(&value, &to, sizeof(value));
    return to;
}
// clang-format on

template <typename T = void>
struct Less {
    constexpr bool operator()(const T& l, const T& r) const
    {
        return l < r;
    }
};

template <>
struct Less<void> {
    using is_transparent = void;

    template <typename L, typename R>
    constexpr auto operator()(L&& l, R&& r) const -> decltype(static_cast<L&&>(l) < static_cast<R&&>(r))
    {
        return static_cast<L&&>(l) < static_cast<R&&>(r);
    }
};

template <typename T>
struct Greater {
    constexpr bool operator()(const T& l, const T& r) const
    {
        return l > r;
    }
};

template <>
struct Greater<void> {
    using is_transparent = void;

    template <typename L, typename R>
    constexpr auto operator()(L&& l, const R&& r) const -> decltype(static_cast<L&&>(l) > static_cast<R&&>(r))
    {
        return static_cast<L&&>(l) > static_cast<R&&>(r);
    }
};

template <typename ItrT, typename U, typename Comparator = Less<>>
ItrT lower_bound(ItrT begin, ItrT end, const U& key, Comparator comparator = Comparator())
{
    ASSERT(begin <= end);

    if (begin == end)
        return end;

    auto* lower_bound = end;

    ssize_t left = 0;
    ssize_t right = end - begin - 1;

    while (left <= right) {
        ssize_t pivot = left + (right - left) / 2;

        auto& pivot_value = begin[pivot];

        if (comparator(key, pivot_value)) {
            lower_bound = &pivot_value;
            right = pivot - 1;
            continue;
        }

        if (comparator(pivot_value, key)) {
            left = pivot + 1;
            continue;
        }

        return &begin[pivot];
    }

    return lower_bound;
}

template <typename ItrT, typename U, typename Comparator = Less<>>
ItrT binary_search(ItrT begin, ItrT end, const U& value, Comparator comparator = Comparator())
{
    auto* result = lower_bound(begin, end, value, comparator);

    return result == end ? result : (comparator(value, *result) ? end : result);
}

template <typename ItrT, typename Pred>
ItrT linear_search_for(ItrT begin, ItrT end, Pred predicate)
{
    for (auto itr = begin; itr != end; ++itr) {
        if (predicate(*itr))
            return itr;
    }

    return end;
}

template <typename ItrT, typename U>
ItrT linear_search(ItrT begin, ItrT end, const U& value)
{
    return linear_search_for(begin, end, [&value](decltype(*begin) cont_val) { return cont_val == value; });
}

template <typename T, typename Comparator = Less<T>>
void quick_sort(T* begin, T* end, Comparator comparator = Comparator())
{
    ASSERT(begin <= end);

    using dp_t = T* (*)(T*, T*, Comparator&);
    using dqs_t = void (*)(T*, T*, Comparator&);

    static dp_t do_partition = [](T* begin, T* end, Comparator& comparator) -> T* {
        ssize_t smallest_index = -1;
        ssize_t array_size = end - begin;

        const ssize_t last_index = array_size - 1;

        auto& pivot = begin[last_index];

        for (ssize_t i = 0; i < last_index; ++i) {
            if (comparator(begin[i], pivot))
                swap(begin[++smallest_index], begin[i]);
        }

        swap(begin[++smallest_index], pivot);
        return &begin[smallest_index];
    };

    static dqs_t do_quick_sort = [](T* begin, T* end, Comparator& comparator) -> void {
        // 1 element or empty array, doesn't make sense to sort
        if (end - begin < 2)
            return;

        auto* pivot = do_partition(begin, end, comparator);

        do_quick_sort(begin, pivot, comparator);
        do_quick_sort(pivot + 1, end, comparator);
    };

    do_quick_sort(begin, end, comparator);
}

template <typename T, typename Comparator = Less<T>>
void insertion_sort(T* begin, T* end, Comparator comparator = Comparator())
{
    ASSERT(begin <= end);

    // 1 element or empty array, doesn't make sense to sort
    if (end - begin < 2)
        return;

    size_t array_size = end - begin;

    for (size_t i = 0; i < array_size; ++i) {
        auto j = i;

        while (j > 0 && comparator(begin[j], begin[j - 1])) {
            swap(begin[j], begin[j - 1]);
            --j;
        }
    }
}
}

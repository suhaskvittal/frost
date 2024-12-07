/*
 *  author: Suhas Vittal
 *  date:   3 Decemeber 2024
 * */

#ifndef UTIL_NUMERICS_h
#define UTIL_NUMERICS_h

#include <cstdint>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr inline size_t _log2(size_t n)
{
    if (n > 1)
        return 1+_log2(n >> 1);
    else
        return 0;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <size_t N>
struct numeric_traits
{
    constexpr static bool   is_power_of_two = (N & (N-1)) == 0;
    constexpr static size_t log2 = _log2(N);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <size_t N, class T> inline T
fast_mod(T x)
{
    if constexpr (numeric_traits<N>::is_power_of_two)
        return static_cast<T>(x & (N-1));
    else
        return static_cast<T>(x % N);
}

template <size_t N, class T> inline void
fast_increment_and_mod_inplace(T& x)
{
    if constexpr (numeric_traits<N>::is_power_of_two) {
        x = (x+1) & (N-1);
    } else {
        ++x;
        if (x == N)  // Should be faster than modulo.
            x = 0;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class T>
constexpr inline T mask(T x)
{
    return x-1;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif   // UTIL_NUMERICS_h

/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef UTIL_STATS_h
#define UTIL_STATS_h

#include <algorithm>
#include <array>
#include <iostream>
#include <iomanip>
#include <string_view>
#include <type_traits>


////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class T, size_t N>
using VecStat = std::array<T, N>;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class T> inline double
mean(T x, T tot)
{
    return static_cast<double>(x) / static_cast<double>(tot);
}

template <class T, size_t N> inline VecStat<double, N> 
mean(const VecStat<T,N>& arr, T tot)
{
    VecStat<double, N> out;
    std::transform(arr.begin(), arr.end(), out.begin(), [] (T x) { return mean(x, tot); });
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr size_t HEADER_WIDTH = 24;
constexpr size_t STAT_NAME_WIDTH = 32;
constexpr size_t STAT_WIDTH = 6;

template <class T>
void print_stat(std::ostream& out, 
        std::string_view header,
        std::string_view stat_name,
        T stat)
{
    out << std::setw(HEADER_WIDTH) << std::left << header
        << std::setw(STAT_NAME_WIDTH) << std::left << stat_name
        << " :";
    if constexpr (std::is_floating_point<T>::value)
        out << std::setprecision(5);
    out << std::setw(STAT_WIDTH) << std::right << stat << "\n";
}

template <class T, size_t N>
void print_vecstat(std::ostream& out,
        std::string_view header,
        std::string_view stat_name,
        T stat)
{
    out << std::setw(HEADER_WIDTH) << std::left << header
        << std::setw(STAT_NAME_WIDTH) << std::left << stat_name
        << ":";
    for (size_t i = 0; i < N; i++) {
        if constexpr (std::is_floating_point<T>::value)
            out << std::setprecision(5);
        out << std::setw(STAT_WIDTH) << std::right << stat;
        if (i < N-1)
            out << ",";
        else
            out << "\n";
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // UTIL_STATS_h

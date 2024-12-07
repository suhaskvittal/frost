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
#include <numeric>
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
    std::transform(arr.begin(), arr.end(), out.begin(), [tot] (T x) { return mean(x, tot); });
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class T, size_t N> inline T
vec_sum(const VecStat<T,N>& arr)
{
    return std::accumulate(arr.begin(), arr.end(), static_cast<T>(0));
}

template <class T, size_t N> inline double
vec_amean(const VecStat<T,N>& arr)
{
    return static_cast<double>(vec_sum(arr)) / N;
}

template <class T, size_t N> inline double
vec_gmean(const VecStat<T,N>& arr)
{
    double log_gmean = std::accumulate(arr.begin(), arr.end(), 0.0,
            [] (T x, T y)
            {
                return x + std::log(static_cast<double>(y));
            });
    return std::exp(log_gmean / N);
}

template <class T, size_t N> inline double
vec_hmean(const VecStat<T,N>& arr)
{
    double denom = std::accumulate(arr.begin(), arr.end(), 0.0,
            [] (T x, T y)
            {
                return x + 1.0/static_cast<double>(y);
            });
    return static_cast<double>(N) / denom;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr size_t HEADER_WIDTH = 12;
constexpr size_t STAT_NAME_WIDTH = 32;
constexpr size_t STAT_WIDTH = 16;

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

enum class VecAccMode { NONE, SUM, AMEAN, GMEAN, HMEAN };

template <class T, size_t N>
void print_vecstat(std::ostream& out,
        std::string_view header,
        std::string_view stat_name,
        const VecStat<T,N>& arr,
        VecAccMode mode = VecAccMode::SUM)
{
    out << std::setw(HEADER_WIDTH) << std::left << header
        << std::setw(STAT_NAME_WIDTH) << std::left << stat_name
        << ":";
    
    for (size_t i = 0; i < N; i++) {
        if constexpr (std::is_floating_point<T>::value)
            out << std::setprecision(5);
        out << std::setw(STAT_WIDTH) << std::right << arr.at(i);
        if (mode != VecAccMode::NONE || i < N-1)
            out << ",";
        else
            out << "\n";
    }
    switch (mode) {
    case VecAccMode::SUM:
        out << std::setw(STAT_WIDTH) << std::right << vec_sum(arr) << "\n";
        break;
    case VecAccMode::AMEAN:
        out << std::setw(STAT_WIDTH) << std::right << vec_amean(arr) << "\n";
        break;
    case VecAccMode::GMEAN:
        out << std::setw(STAT_WIDTH) << std::right << vec_gmean(arr) << "\n";
        break;
    case VecAccMode::HMEAN:
        out << std::setw(STAT_WIDTH) << std::right << vec_hmean(arr) << "\n";
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // UTIL_STATS_h

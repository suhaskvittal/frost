/*
 *  author: Suhas Vittal
 *  date:   11 February 2025
 * */

#ifndef DRAM_STATS_h
#define DRAM_STATS_h

#include "util/stats.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

using write_counts_array = std::array<size_t, DRAM_TOT_BANKS_PER_CHANNEL>;

inline void dram_update_write_distribution_stats(const write_counts_array& cnts, double& s_std, uint32_t& s_diff)
{
    s_std += vec_std(cnts);

    auto [min_it, max_it] = std::minmax_element(cnts.begin(), cnts.end());
    s_diff += (*max_it) - (*min_it);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_STATS_h

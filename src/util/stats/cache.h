/*
 *  author: Suhas Vittal
 *  date:   12 December 2024
 * */

#ifndef UTIL_STATS_CACHE_h
#define UTIL_STATS_CACHE_h

#include "constants.h"
#include "util/stats.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
print_cache_stats_for_core_header(std::ostream& out)
{
    out << std::setw(16) << std::left << "CACHE"
        << std::setw(16) << std::left << "ACCESSES"
        << std::setw(16) << std::left << "MISSES"
        << std::setw(16) << std::left << "MISS_RATE"
        << std::setw(16) << std::left << "INVALIDATES"
        << std::setw(16) << std::left << "WRITE_ALLOC"
        << std::setw(16) << std::left << "APKI"
        << std::setw(16) << std::left << "MPKI"
        << std::setw(16) << std::left << "AAT"
        << std::setw(16) << std::left << "MISS_PENALTY"
        << std::setw(16) << std::left << "WRITEBACKS"
        << std::setw(16) << std::left << "WRITE_BLOCKED"
        << "\n" << BAR << "\n";
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
 
template <class CORE, class CACHE> inline void
print_cache_stats_for_core(CORE* c, const std::unique_ptr<CACHE>& cache, std::ostream& out, std::string_view header)
{
    uint8_t id = c->coreid_;
    uint64_t inst = c->finished_inst_num_;
    
    uint64_t accesses = cache->s_accesses_.at(id),
             misses = cache->s_misses_.at(id),
             invalidates = cache->s_invalidates_.at(id),
             write_alloc = cache->s_write_alloc_.at(id);

    double apki = mean(accesses, inst) * 1000.0,
           mpki = mean(misses, inst) * 1000.0;
    double miss_penalty = misses == 0 ? 0.0 : mean(cache->s_tot_penalty_.at(id), cache->s_num_penalty_.at(id));
    double miss_rate = mean(misses, accesses);
    double aat = CACHE::CACHE_LATENCY * (1-miss_rate) + miss_penalty*miss_rate;

    uint64_t write_blocked_cycles = cache->io_->s_blocking_writes_ / CACHE::NUM_RW_PORTS;

    out << std::setw(16) << std::left << header
        << std::setw(16) << std::left << accesses
        << std::setw(16) << std::left << misses
        << std::setw(16) << std::left << std::setprecision(3) << miss_rate
        << std::setw(16) << std::left << invalidates
        << std::setw(16) << std::left << write_alloc
        << std::setw(16) << std::left << std::setprecision(3) << apki
        << std::setw(16) << std::left << std::setprecision(3) << mpki
        << std::setw(16) << std::left << std::setprecision(3) << aat
        << std::setw(16) << std::left << std::setprecision(3) << miss_penalty
        << std::setw(16) << std::left << cache->s_writebacks_
        << std::setw(16) << std::left << write_blocked_cycles
        << "\n";
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // UTIL_STATS_CACHE_h

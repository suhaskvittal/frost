/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "util/numerics.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline size_t dram_channel(uint64_t x)
{
    return (x >> MOP) & mask(DRAM_CHANNELS);
}

constexpr size_t BG_OFF = MOP + numeric_traits<DRAM_CHANNELS>::log2;
constexpr size_t BA_OFF = BG_OFF + numeric_traits<DRAM_BANKGROUPS>::log2;
constexpr size_t RA_OFF = BA_OFF + numeric_traits<DRAM_RANKS>::log2;
constexpr size_t ROW_OFF = numeric_limits<DRAM_COLUMNS>::log2
                            + numeric_limits<DRAM_CHANNELS>::log2
                            + numeric_limits<DRAM_BANKGROUPS>::log2
                            + numeric_limits<DRAM_BANKS>::log2
                            + numeric_limits<DRAM_RANKS>::log2;

inline size_t dram_bankgroup(uint64_t x)
{
    return (x >> BG_OFF) & mask(DRAM_BANKGROUPS); 
}

inline size_t dram_bank(uint64_t x)
{
    return (x >> BA_OFF) & mask(DRAM_BANKS);
}

inline size_t dram_rank(uint64_t x)
{
    return (x >> RA_OFF) & mask(DRAM_RANKS);
}

inline size_t dram_row(uint64_t x)
{
    return (x >> ROW_OFF) & mask(DRAM_ROWS);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

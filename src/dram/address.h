/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef DRAM_ADDRESS_h
#define DRAM_ADDRESS_h

#include "constants.h"
#include "util/numerics.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

size_t dram_channel(uint64_t);
size_t dram_rank(uint64_t);
size_t dram_bankgroup(uint64_t);
size_t dram_bank(uint64_t);
size_t dram_row(uint64_t);
size_t dram_column(uint64_t);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <size_t FROM, size_t SIZE>
bool bit_is_in_region(size_t);

constexpr size_t dram_col_bit_index(size_t);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#if defined(DRAM_AM_MOP)

#include "address/mop.inl"

#elif defined(DRAM_AM_COFFEELAKE)

#include "address/coffeelake.inl"

#elif defined(DRAM_AM_SKYLAKE)

#include "address/skylake.inl"

#elif defined(DRAM_AM_ZEN)

#include "address/zen.inl"

#elif defined(DRAM_AM_RANDOM)

#include "address/random.inl"

#endif

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline size_t dram_bank_idx(uint64_t addr)
{
    return dram_bank(addr) 
            + dram_bankgroup(addr)*DRAM_BANKS 
            + dram_rank(addr)*DRAM_BANKGROUPS*DRAM_BANKS;
}

inline size_t dram_bankgroup_idx(uint64_t addr)
{
    return dram_bankgroup(addr) + dram_rank(addr)*DRAM_BANKGROUPS;
}

inline uint64_t dram_get_column_neighbor(uint64_t addr, size_t b)
{
    return addr ^ (1L << dram_col_bit_index(b));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_ADDRESS_h

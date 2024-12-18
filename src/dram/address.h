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

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#if defined(DRAM_AM_MOP)
#include "address/mop.inl"
#elif defined(DRAM_AM_COFFEELAKE)
#include "address/coffeelake.inl"
#elif defined(DRAM_AM_SKYLAKE)
#include "address/skylake.inl"
#endif

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline size_t get_bank_idx(uint64_t addr)
{
    return dram_bank(addr) 
            + dram_bankgroup(addr)*DRAM_BANKS 
            + dram_rank(addr)*DRAM_BANKGROUPS*DRAM_BANKS;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_ADDRESS_h

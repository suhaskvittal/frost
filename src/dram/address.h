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

size_t dram_rank_base(uint64_t);
size_t dram_bankgroup_base(uint64_t);
size_t dram_bank_base(uint64_t);

size_t dram_channel(uint64_t);
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

#elif defined(DRAM_AM_TEST)

#include "address/testmap.inl"

#endif

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/*
 * Need to include in a separate file to avoid circular dependences:
 * */
size_t permute_with_tag(size_t a, uint64_t x, size_t count, size_t offset);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/*
 * Depending on whether or not we are using a permutation-based address mapping,
 * we will define `dram_bank` (etc.) accordingly:
 * */

#if defined(DRAM_AM_ENABLE_PERMUTATION)

inline size_t dram_rank(uint64_t x)
{
    return permute_with_tag(dram_rank_base(x), x, DRAM_RANKS, DRAM_BANKGROUPS*DRAM_BANKS);
}

inline size_t dram_bankgroup(uint64_t x)
{
    return permute_with_tag(dram_bankgroup_base(x), x, DRAM_BANKGROUPS, DRAM_BANKS);
}

inline size_t dram_bank(uint64_t x)
{
    return permute_with_tag(dram_bank_base(x), x, DRAM_BANKS, 1);
}

#else

inline size_t dram_rank(uint64_t x) { return dram_rank_base(x); };
inline size_t dram_bankgroup(uint64_t x) { return dram_bankgroup_base(x); };
inline size_t dram_bank(uint64_t x) { return dram_bank_base(x); };

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

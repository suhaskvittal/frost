/*
 *  author: Suhas Vittal
 *  date:   25 February 2025
 * */

#include "cache/indexing.h"
#include "dram/address.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

size_t
ssrh_cache_set_index(uint64_t x, size_t s)
{
    size_t idx = 0;

    size_t bitcount = 0;

    size_t ii = 0;
    for (size_t i = 0; i < OPT_SSRH_COLUMN_COUNT; i++)
    {
        size_t col = dram_col_bit_index(i);
        size_t d = col - ii;

        // Add bits `ii` to `col` (exclusive of `col`) to index:
        idx |= ((x >> ii) & ((1L << d) - 1)) << bitcount;

        bitcount += d;
        ii = col+1;
    }

    idx |= (x >> ii) << bitcount; 
    return fast_mod(idx, s);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/*
 * Provided that `CH_OFF`, `BG_OFF`, etc. are defined,
 * and all these bits are contiguous (column bits need not
 * be contiguous), this file will provide appropriate
 * address mapping functions.
 *
 * See `mop.inl` or `coffeelake.inl` for an example.
 * */

inline size_t dram_channel(uint64_t x)
{
    return (x >> CH_OFF) & mask(DRAM_CHANNELS);
}

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

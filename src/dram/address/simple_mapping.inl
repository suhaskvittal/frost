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

#include "globals.h"

#include <iostream>
#include <iomanip>
#include <unordered_set>

inline void
print_address_mapping(std::ostream& out)
{
    // First print out bit nums
    out << "Address Mapping:\n" << BAR << "\n";
    for (size_t i = 0; i <= 48; i += 6) {
        out << std::setw(18) << std::left << i;
    }
    out << "\n";
    // First print out indicators of page bits and line bits.
    for (size_t i = 0; i < 48; i++) {
        if (i < numeric_traits<LINESIZE>::log2)
            out << ".  ";
        else if (i < numeric_traits<PAGESIZE>::log2)
            out << "li ";
        else
            out << "pg ";
    }
    out << "\n";
    // Now print out parts of dram address mapping.
    std::unordered_set<size_t> endpoints{
        CH_OFF, RA_OFF, BG_OFF, BA_OFF, ROW_OFF,
        CH_OFF+numeric_traits<DRAM_CHANNELS>::log2,
        RA_OFF+numeric_traits<DRAM_RANKS>::log2,
        BG_OFF+numeric_traits<DRAM_BANKGROUPS>::log2,
        BA_OFF+numeric_traits<DRAM_BANKS>::log2,
        ROW_OFF+numeric_traits<DRAM_ROWS>::log2
    };
#define BETWEEN(x, A, B) ((x) >= (A) && (x) < (A) + numeric_traits<B>::log2)
    for (size_t i = 0; i < numeric_traits<LINESIZE>::log2; i++)
        out << ".  ";
    for (size_t i = 0; i < 48 - numeric_traits<LINESIZE>::log2; i++) {
        if (BETWEEN(i, CH_OFF, DRAM_CHANNELS))
            out << "ch ";
        else if (BETWEEN(i, RA_OFF, DRAM_RANKS))
            out << "ra ";
        else if (BETWEEN(i, BG_OFF, DRAM_BANKGROUPS))
            out << "bg ";
        else if (BETWEEN(i, BA_OFF, DRAM_BANKS))
            out << "ba ";
        else if (BETWEEN(i, ROW_OFF, DRAM_ROWS))
            out << "ro ";
        else if (i < ROW_OFF)
            out << "co ";
    }
    out << "\n";
#undef BETWEEN
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

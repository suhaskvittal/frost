
#include <iostream>
#include <iomanip>
#include <random>
#include <unordered_map>
#include <unordered_set>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline uint64_t deterministic_random_number(uint64_t x)
{
    using rng_type = std::mt19937_64;
    using memo_type = std::unordered_map<uint64_t, uint64_t>;

    static rng_type rng{1234};
    static memo_type memo;

    if (!memo.count(x))
        memo[x] = rng();
    return memo[x];
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr size_t CH_OFF = numeric_traits<DRAM_COLUMNS>::log2;
constexpr size_t BG_OFF = CH_OFF + numeric_traits<DRAM_CHANNELS>::log2;
constexpr size_t BA_OFF = BG_OFF + numeric_traits<DRAM_BANKGROUPS>::log2;
constexpr size_t RA_OFF = BA_OFF + numeric_traits<DRAM_BANKS>::log2;
constexpr size_t ROW_OFF = RA_OFF + numeric_traits<DRAM_RANKS>::log2;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#define RANDOM  deterministic_random_number(x)

inline size_t dram_channel(uint64_t x)
{
    return (RANDOM >> CH_OFF) & mask(DRAM_CHANNELS);
}

inline size_t dram_bankgroup(uint64_t x)
{
    return (RANDOM >> BG_OFF) & mask(DRAM_BANKGROUPS); 
}

inline size_t dram_bank(uint64_t x)
{
    return (RANDOM >> BA_OFF) & mask(DRAM_BANKS);
}

inline size_t dram_rank(uint64_t x)
{
    return (RANDOM >> RA_OFF) & mask(DRAM_RANKS);
}

inline size_t dram_row(uint64_t x)
{
    return (RANDOM >> ROW_OFF) & mask(DRAM_ROWS);
}

#undef RANDOM

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <size_t FROM, size_t SIZE>
inline bool bit_is_in_region(size_t x)
{
    return x >= FROM && x < FROM + numeric_traits<SIZE>::log2;
}

inline constexpr size_t dram_lowest_col_bit_index(void)
{
    for (size_t i = 0; i < numeric_traits<DRAM_SIZE_MB*1024*1024>::log2; i++)
    {
        if (bit_is_in_region<CH_OFF, DRAM_CHANNELS>(i)
            || bit_is_in_region<RA_OFF, DRAM_RANKS>(i)
            || bit_is_in_region<BG_OFF, DRAM_BANKGROUPS>(i)
            || bit_is_in_region<BA_OFF, DRAM_BANKS>(i)
            || bit_is_in_region<ROW_OFF, DRAM_ROWS>(i))
        {
            continue;
        }
        return i;
    }
    std::cerr << "column bit not found: invalid dram address mapping.\n";
    exit(1);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline void
print_address_mapping(std::ostream& out)
{
    // First print out bit nums
    out << "Address Mapping:\n" << BAR << "\n";
    for (size_t i = 0; i <= 48; i += 6) 
    {
        out << std::setw(18) << std::left << i;
    }
    out << "\n";
    // First print out indicators of page bits and line bits.
    for (size_t i = 0; i < 48; i++)
    {
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
    for (size_t i = 0; i < numeric_traits<LINESIZE>::log2; i++)
        out << ".  ";
    for (size_t i = 0; i < 48 - numeric_traits<LINESIZE>::log2; i++) 
    {
        if (bit_is_in_region<CH_OFF, DRAM_CHANNELS>(i))
            out << "ch ";
        else if (bit_is_in_region<RA_OFF, DRAM_RANKS>(i))
            out << "ra ";
        else if (bit_is_in_region<BG_OFF, DRAM_BANKGROUPS>(i))
            out << "bg ";
        else if (bit_is_in_region<BA_OFF, DRAM_BANKS>(i))
            out << "ba ";
        else if (bit_is_in_region<ROW_OFF, DRAM_ROWS>(i))
            out << "ro ";
        else if (i < ROW_OFF)
            out << "co ";
    }
    out << "\n";
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

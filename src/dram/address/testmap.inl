///    6                 12
///    co ch bg co co co bg bg ba ba co co ro ...
///

#include <iostream>

constexpr size_t NUM_BG_LOW = 1;
constexpr size_t NUM_COL_LOW = 6 - ilog2(DRAM_CHANNELS) - NUM_BG_LOW;

constexpr size_t CH_OFF = 1;
constexpr size_t BG_LOW_OFF = CH_OFF + ilog2(DRAM_CHANNELS);
constexpr size_t BG_HIGH_OFF = 6;
constexpr size_t BA_OFF = ilog2(DRAM_CHANNELS) + ilog2(DRAM_BANKGROUPS) + NUM_COL_LOW;
constexpr size_t RA_OFF = BA_OFF + ilog2(DRAM_BANKS);
constexpr size_t ROW_OFF = ilog2(DRAM_COLUMNS)
                            + ilog2(DRAM_CHANNELS)
                            + ilog2(DRAM_BANKGROUPS)
                            + ilog2(DRAM_BANKS)
                            + ilog2(DRAM_RANKS);
                            
inline size_t dram_channel(uint64_t x)
{
    return (x >> CH_OFF) & (DRAM_CHANNELS-1);
}

inline size_t dram_bankgroup(uint64_t x)
{
    size_t lo = (x >> BG_LOW_OFF) & ((1<<NUM_BG_LOW)-1);
    size_t hi = (x >> BG_HIGH_OFF) & ((DRAM_BANKGROUPS >> NUM_BG_LOW) - 1);

    return lo | (hi << NUM_BG_LOW);
}

inline size_t dram_bank(uint64_t x)
{
    return (x >> BA_OFF) & (DRAM_BANKS-1);
}

inline size_t dram_rank(uint64_t x)
{
    return (x >> RA_OFF) & (DRAM_RANKS-1);
}

inline size_t dram_row(uint64_t x)
{
    return (x >> ROW_OFF) & (DRAM_ROWS-1);
}

constexpr inline size_t
dram_col_bit_index(size_t idx)
{
    if (idx == 0)
        return 0;
    else if (idx >= 1 && idx < NUM_COL_LOW)
        return NUM_BG_LOW + ilog2(DRAM_CHANNELS) + (idx-1);
    else
        return RA_OFF + ilog2(DRAM_RANKS) + (idx-NUM_COL_LOW);
}

inline void
print_address_mapping(std::ostream& out)
{
    std::cout << "TEST MAPPING\n";
}

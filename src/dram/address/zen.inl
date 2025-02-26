constexpr size_t CH_OFF = 0;
constexpr size_t BG_OFF = CH_OFF + ilog2(DRAM_CHANNELS) + 1;
constexpr size_t BA_OFF = BG_OFF + ilog2(DRAM_BANKGROUPS);
constexpr size_t RA_OFF = BA_OFF + ilog2(DRAM_BANKS);
constexpr size_t ROW_OFF = ilog2(DRAM_COLUMNS)
                            + ilog2(DRAM_CHANNELS)
                            + ilog2(DRAM_BANKGROUPS)
                            + ilog2(DRAM_BANKS)
                            + ilog2(DRAM_RANKS);

inline size_t dram_column(uint64_t x)
{
    size_t lower = x & 1,
           upper = (x >> (RA_OFF + ilog2(DRAM_RANKS))) & (DRAM_COLUMNS/2-1);
    return lower | (upper << 1);
}

#include "simple_mapping.inl"

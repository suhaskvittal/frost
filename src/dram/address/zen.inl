constexpr size_t CH_OFF = 0;
constexpr size_t BG_OFF = CH_OFF + numeric_traits<DRAM_CHANNELS>::log2 + 1;
constexpr size_t BA_OFF = BG_OFF + numeric_traits<DRAM_BANKGROUPS>::log2;
constexpr size_t RA_OFF = BA_OFF + numeric_traits<DRAM_BANKS>::log2;
constexpr size_t ROW_OFF = numeric_traits<DRAM_COLUMNS>::log2
                            + numeric_traits<DRAM_CHANNELS>::log2
                            + numeric_traits<DRAM_BANKGROUPS>::log2
                            + numeric_traits<DRAM_BANKS>::log2
                            + numeric_traits<DRAM_RANKS>::log2;

inline size_t dram_column(uint64_t x)
{
    size_t lower = x & 1,
           upper = (x >> (RA_OFF + numeric_traits<DRAM_RANKS>::log2)) & (DRAM_COLUMNS/2-1);
    return lower | (upper << 1);
}

#include "simple_mapping.inl"

constexpr size_t CH_OFF = numeric_traits<DRAM_AM_MOP>::log2;
constexpr size_t BG_OFF = CH_OFF + numeric_traits<DRAM_CHANNELS>::log2;
constexpr size_t BA_OFF = BG_OFF + numeric_traits<DRAM_BANKGROUPS>::log2;
constexpr size_t RA_OFF = BA_OFF + numeric_traits<DRAM_RANKS>::log2;
constexpr size_t ROW_OFF = numeric_traits<DRAM_COLUMNS>::log2
                            + numeric_traits<DRAM_CHANNELS>::log2
                            + numeric_traits<DRAM_BANKGROUPS>::log2
                            + numeric_traits<DRAM_BANKS>::log2
                            + numeric_traits<DRAM_RANKS>::log2;

#include "simple_mapping.inl"

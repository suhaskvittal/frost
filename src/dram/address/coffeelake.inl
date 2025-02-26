constexpr size_t CH_OFF = ilog2(DRAM_COLUMNS);
constexpr size_t BG_OFF = CH_OFF + ilog2(DRAM_CHANNELS);
constexpr size_t BA_OFF = BG_OFF + ilog2(DRAM_BANKGROUPS);
constexpr size_t RA_OFF = BA_OFF + ilog2(DRAM_BANKS);
constexpr size_t ROW_OFF = RA_OFF + ilog2(DRAM_RANKS);

#include "simple_mapping.inl"

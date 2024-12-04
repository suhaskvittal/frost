#ifndef CONSTANTS_h
#define CONSTANTS_h

constexpr size_t LINESIZE = 64;
constexpr size_t PAGESIZE = 4096;

constexpr size_t DRAM_CHANNELS = 2;
constexpr size_t DRAM_RANKS = 1;
constexpr size_t DRAM_BANKGROUPS = 8;
constexpr size_t DRAM_BANKS = 4;
constexpr size_t DRAM_ROWS = 65536;
constexpr size_t DRAM_COLUMNS = 128;

constexpr size_t DRAM_BURST_LENGTH = 16;

constexpr size_t DRAM_RQ_SIZE = 128;
constexpr size_t DRAM_WQ_SIZE = 128;
constexpr size_t DRAM_CMDQ_SIZE = 16;

constexpr size_t DRAM_SIZE_MB = DRAM_CHANNELS
                                    * DRAM_RANKS
                                    * DRAM_BANKGROUPS
                                    * DRAM_ROWS
                                    * DRAM_COLUMNS
                                    * LINESIZE;

#endif  // CONSTANTS_h

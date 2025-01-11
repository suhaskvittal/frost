#ifndef DRAM_CMD_ARGS_h
#define DRAM_CMD_ARGS_h

#include <cstdint>

// These are only used if `DRAM_USE_WATERMARKS_TO_DRAIN` is defined.
extern double   OPT_DRAM_LOW_WATERMARK;
extern double   OPT_DRAM_HIGH_WATERMARK;

extern uint64_t OPT_DRAM_WRITE_SYNC_COUNT;

#endif  // DRAM_CMD_ARGS_h

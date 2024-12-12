/*
 *  author: Suhas Vittal
 *  date:   11 December 2024
 * */

#ifndef OS_FREE_LIST_h
#define OS_FREE_LIST_h

#include "constants.h"

#include <array>
#include <cstdint>
#include <random>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr size_t NUM_PAGE_FRAMES = (DRAM_SIZE_MB*1024*1024) / PAGESIZE;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class FreeList
{
public:
    uint64_t s_page_faults_ =0;
private:
    constexpr static size_t BITVEC_WIDTH = NUM_PAGE_FRAMES/64;

    using free_bitvec_t = std::array<uint64_t, BITVEC_WIDTH>;
    /*
     * Page frame management. `free_page_frames_` uses active-low as available,
     * and `rng` is used for randomized page allocation.
     * */
    free_bitvec_t free_page_frames_{};
    std::mt19937_64 rng_{0};
public:
    FreeList(void) =default;
    /*
     * Searches for a free, random page frame. If found, then returns this pfn.
     * Otherwise, prints to `stderr` and exits with code 1.
     * */
    uint64_t get_and_reserve_free_page_frame(void);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // OS_FREE_LIST_h

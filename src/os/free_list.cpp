/*
 *  author: Suhas Vittal
 *  date:   11 December 2024
 * */

#include "os/free_list.h"
#include "util/numerics.h"

#include <cstddef>
#include <iostream>
#include <numeric>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

uint64_t
FreeList::get_and_reserve_free_page_frame()
{
    ++s_page_faults_;
    for (size_t i = 0; i < 2048; i++) {
        size_t pfn = fast_mod<NUM_PAGE_FRAMES>(rng_());
        size_t ii = pfn >> 6,
               jj = pfn & 0x3f;
        bool is_taken = free_page_frames_[ii] & (1L << jj);
        if (!is_taken) {
            free_page_frames_[ii] |= (1L << jj);
            return pfn;
        }
    }
    size_t num_free = std::accumulate(free_page_frames_.begin(), free_page_frames_.end(), 0ull,
                                        [] (uint64_t x, uint64_t y)
                                        {
                                            return x + __builtin_popcount(~y);
                                        });
    std::cerr << "free list: failed to find free page frame, free page frames: " << num_free
        << ", total = " << NUM_PAGE_FRAMES << "\n";
    exit(1);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

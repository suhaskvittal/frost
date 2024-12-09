/*
 *  author: Suhas Vittal
 *  date:   8 December 2024
 * */

#include "util/numerics.h"
#include "vmem.h"

#include <algorithm>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

VirtualMemory::VirtualMemory(uint64_t ptbr)
    :ptbr_(ptbr)
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

VirtualMemory::walk_result_t
VirtualMemory::do_page_walk(uint64_t vpn)
{
    walk_result_t out;

    // Compute level indices for the page.
    std::array<size_t, PT_LEVELS> levels;
    for (size_t i = 0; i < PT_LEVELS; i++) {
        levels[i] = fast_mod<NUM_PTE_PER_TABLE>(vpn);
        vpn >>= numeric_traits<NUM_PTE_PER_TABLE>::log2;
    }

    // Try to access each level.
    out[1] = PAGE_TABLE_BASE_REGISTER;
    size_t k = 2;

    page_table_t& curr_pt = base_pt_;
    for (size_t i = PT_LEVELS-1; i > 0; i--) {
        pte_ptr& e = access_entry_and_alloc_if_dne(curr_pt, levels[i]);
        if (e->next == nullptr) {
            e->next = page_table_ptr(new page_table_t);
            e->next->fill(nullptr);
        }
        curr_pt = *e->next;
        out[k++] = e->pfn;
    }
    // At the lowest level, which will have the pfn.
    pte_ptr& e = access_entry_and_alloc_if_dne(curr_pt, levels[0]);
    out[0] = e->pfn;
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

uint64_t
VirtualMemory::get_and_reserve_free_page_frame()
{
    ++s_page_faults_;
    for (size_t i = 0; i < 1024; i++) {
        size_t pfn = fast_mod<NUM_PAGE_FRAMES>(rng());
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
    std::cerr << "os: failed to find free page frame, free page frames: " << num_free << "\n";
    exit(1);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

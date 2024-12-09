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
    out[2] = levels[PT_LEVELS-1];
    size_t k = 3;

    page_table_t& curr_pt = base_pt_;
    for (size_t i = PT_LEVELS-1; i > 0; i--) {
        pte_ptr& e = access_entry_and_alloc_if_dne(curr_pt, levels[i]);
        if (e->next == nullptr) {
            e->next = page_table_ptr(new page_table_t);
            e->next->fill(nullptr);
        }
        curr_pt = *e->next;
        out[k] = e->pfn;
        out[k+1] = levels[i-1];
        k += 2;
    }
    // At the lowest level, which will have the pfn.
    pte_ptr& e = access_entry_and_alloc_if_dne(curr_pt, levels[0]);
    out[0] = e->pfn;
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

pte_ptr&
VirtualMemory::access_entry_and_alloc_if_dne(page_table_t& pt, size_t idx)
{
    if (pt[idx] == nullptr)
        pt[idx] = make_new_pte();
    return pt[idx];
}

pte_ptr&&
VirtualMemory::make_new_pte()
{
    pte_ptr e = pte_ptr(new PageTableEntry);
    e->pfn = GL_OS->get_and_reserve_free_page_frame();
    return e;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

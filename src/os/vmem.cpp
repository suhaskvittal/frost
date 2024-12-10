/*
 *  author: Suhas Vittal
 *  date:   8 December 2024
 * */

#include "os.h"
#include "os/vmem.h"
#include "util/numerics.h"

#include <algorithm>
#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

VirtualMemory::VirtualMemory(uint64_t ptbr)
    :ptbr_(ptbr),
    base_pt_(new page_table_t)
{
    base_pt_->fill(nullptr);
}

void
free_pt(page_table_ptr pt)
{
    for (size_t i = 0; i < pt->size(); i++) {
        pte_ptr e = pt->at(i);
        if (e == nullptr)
            continue;
        if (e->next != nullptr) {
            free_pt(e->next);
        }
        delete e;
    }
    delete pt;
}

VirtualMemory::~VirtualMemory()
{
    free_pt(base_pt_);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

VirtualMemory::walk_result_t
VirtualMemory::do_page_walk(uint64_t vpn)
{
    walk_result_t out;

    // Compute level indices for the page.
    std::array<size_t, PT_LEVELS> levels;
    uint64_t bits = vpn;
    for (size_t i = 0; i < PT_LEVELS; i++) {
        levels[i] = fast_mod<NUM_PTE_PER_TABLE>(bits);
        bits >>= numeric_traits<NUM_PTE_PER_TABLE>::log2;
    }

    // Try to access each level.
    out[1] = ptbr_;
    out[2] = levels[PT_LEVELS-1];
    size_t k = 3;

    page_table_ptr curr_pt = base_pt_;
    for (size_t i = PT_LEVELS-1; i > 0; i--) {
        pte_ptr e = access_entry_and_alloc_if_dne(curr_pt, levels[i]);
        if (e->next == nullptr) {
            e->next = page_table_ptr(new page_table_t);
            e->next->fill(nullptr);
        }
        curr_pt = e->next;
        out[k] = e->pfn;
        out[k+1] = levels[i-1];
        k += 2;
    }
    // At the lowest level, which will have the pfn.
    pte_ptr e = access_entry_and_alloc_if_dne(curr_pt, levels[0]);
    out[0] = e->pfn;
    vpn_to_pfn_memo_[vpn] = e->pfn;
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

pte_ptr
VirtualMemory::access_entry_and_alloc_if_dne(page_table_ptr pt, size_t idx)
{
    if (pt->at(idx) == nullptr) {
        pt->at(idx) = make_new_pte();
    }
    return pt->at(idx);
}

pte_ptr
VirtualMemory::make_new_pte()
{
    pte_ptr e = pte_ptr(new PageTableEntry);
    e->pfn = GL_OS->get_and_reserve_free_page_frame();
    return e;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

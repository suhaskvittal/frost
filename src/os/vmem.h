/*
 *  author: Suhas Vittal
 *  date:   8 December 2024
 * */

#ifndef OS_VMEM_h
#define OS_VMEM_h

#include "constants.h"
#include "globals.h"

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct PageTableEntry;

using pte_ptr        = PageTableEntry*;
using page_table_t   = std::array<pte_ptr, NUM_PTE_PER_TABLE>; 
using page_table_ptr = page_table_t*;
/*
 * If `next` is null, then this points to a virtual page. Otherwise,
 * it points to another page table.
 * */
struct PageTableEntry
{
    page_table_ptr next =nullptr;
    uint64_t       pfn;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * Models a multi-level page table (with `PT_LEVELS` levels).
 * */
class VirtualMemory
{
public:
    uint64_t s_page_faults_ =0;
    
    const uint64_t ptbr_;
private:
    using memo_t = std::unordered_map<uint64_t, uint64_t>;

    page_table_ptr base_pt_;
    /*
     * This is just for fast lookups (memoization) when we only want the pfn
     * but don't want to do a page walk.
     * */
    memo_t vpn_to_pfn_memo_;
public:
    /*
     * `walk_result_t` will have the structure where the first entry is the page
     * frame number of the requested VPN, and the remaining entries are the
     * physical addresses of the accessed page tables from highest level (i.e., `base_pt_`)
     * to lowest level, and the offsets into the tables.
     *
     * For instance, the layout is as such:
     *  [0] = pfn
     *  [1] = pfn of `base_pt_`
     *  [2] = page offset into `base_pt`.
     *  (etc.)
     * */
    using walk_result_t = std::array<uint64_t, 2*PT_LEVELS+1>;

    VirtualMemory(uint64_t ptbr);
    ~VirtualMemory(void);
    /*
     * Performs a walk starting from `base_pt_` to find the page frame of `vpn`.
     * Returns the page frames according to `walk_result_t`.
     *
     * If the translation does not exist for `vpn`, we allocate page tables
     * and entries for `vpn`.
     * */
    walk_result_t do_page_walk(uint64_t vpn);
    /*
     * Return the `pfn` for the given `vpn`. If the mapping is not memoized, then
     * we perform a page walk to compute the `pfn`.
     * */
    inline uint64_t get_pfn(uint64_t vpn)
    {
        if (!vpn_to_pfn_memo_.count(vpn))
            do_page_walk(vpn);
        return vpn_to_pfn_memo_[vpn];
    }
private:
    pte_ptr access_entry_and_alloc_if_dne(page_table_ptr, size_t idx);
    pte_ptr make_new_pte(void);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // OS_VMEM_h

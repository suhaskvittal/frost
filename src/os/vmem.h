/*
 *  author: Suhas Vittal
 *  date:   8 December 2024
 * */

#ifndef OS_VMEM_h
#define OS_VMEM_h

#include "globals.h"

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr uint64_t PAGE_TABLE_BASE_REGISTER = 0;
constexpr size_t NUM_PAGE_FRAMES = (DRAM_SIZE_MB*1024*1024) / PAGESIZE;
constexpr size_t NUM_PTE_PER_TABLE = PAGESIZE/PTESIZE;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct PageTableEntry;

using pte_ptr        = std::unique_ptr<PageTableEntry>;
using page_table_t   = std::array<pte_ptr, NUM_PTE_PER_TABLE>; 
using page_table_ptr = std::unique_ptr<page_table_t>;
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
private:
    constexpr static size_t BITVEC_WIDTH = NUM_PAGE_FRAMES/64;

    using memo_t = std::unordered_map<uint64_t, uint64_t>;
    using free_bitvec_t = std::array<uint64_t, BITVEC_WIDTH>;

    page_table_t  base_pt_{};
    free_bitvec_t free_page_frames_{};  // Note that active-low means available.
    /*
     * This is just for fast lookups (memoization) when we only want the pfn
     * but don't want to do a page walk.
     * */
    memo_t vpn_to_fpn_memo_;
    /*
     * `rng` is for page frame allocation.
     * */
    std::mt19937_64 rng{0};
public:
    VirtualMemory(void) =default;
    /*
     * `walk_result_t` will have the structure where the first entry is the page
     * frame number of the requested VPN, and the remaining entries are the
     * physical addresses of the accessed page tables from highest level (i.e., `base_pt_`)
     * to lowest level.
     * */
    using walk_result_t = std::array<uint64_t, PT_LEVELS+1>;
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
            vpn_to_pfn_memo_[vpn] = do_page_walk(vpn)[0];
        return vpn_to_pfn_memo_[vpn];
    }
private:
    /*
     * Searches for a free, random page frame. If found, then returns this pfn.
     * Otherwise, prints to `stderr` and exits with code 1.
     * */
    uint64_t get_and_reserve_free_page_frame(void);
    /*
     * Useful inlines:
     * */
    inline pte_ptr& access_entry_and_alloc_if_dne(page_table_t& pt, size_t idx)
    {
        if (pt[idx] == nullptr)
            pt[idx] = make_new_pte();
        return pt[idx];
    }

    inline pte_ptr&& make_new_pte(void)
    {
        pte_ptr e = pte_ptr(new PageTableEntry);
        e->pfn = get_and_reserve_free_page_frame();
        return e;
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // OS_VMEM_h

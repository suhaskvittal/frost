/* author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef OS_h
#define OS_h

#include "constants.h"
#include "core/instruction.h"
#include "os/ptw.h"
#include "os/vmem.h"
#include "util/numerics.h"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <random>
#include <unordered_map>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr size_t NUM_PAGE_FRAMES = (DRAM_SIZE_MB*1024*1024) / PAGESIZE;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct ITLB;
struct DTLB;
struct L2LTB;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class OS
{
public:
    using itlb_ptr  = std::unique_ptr<ITLB>;
    using dtlb_ptr  = std::unique_ptr<DTLB>;
    using l2tlb_ptr = std::unique_ptr<L2TLB>;

    using itlb_array_t = std::array<itlb_ptr, NUM_THREADS>;
    using dtlb_array_t = std::array<dtlb_ptr, NUM_THREADS>;
    using l2tlb_array_t = std::array<l2tlb_ptr, NUM_THREADS>;

    itlb_array_t  ITLB_;
    dtlb_array_t  DTLB_;
    l2tlb_array_t L2TLB_;

    uint64_t s_page_faults_ =0;
private:
    constexpr static size_t BITVEC_WIDTH = NUM_PAGE_FRAMES/64;

    using vmem_ptr = std::unique_ptr<VirtualMemory>;
    using vmem_array_t = std::array<vmem_ptr, NUM_THREADS>;

    using ptw_ptr = std::unique_ptr<PageTableWalker>;
    using ptw_array_t = std::array<ptw_ptr, NUM_THREADS>;

    using free_bitvec_t = std::array<uint64_t, BITVEC_WIDTH>;
    /*
     * Each core has its own virtual memory and page walker (`CoreMMU`):
     * */
    vmem_array_t vmem_;
    ptw_array_t  ptw_;
    /*
     * Page frame management. `free_page_frames_` uses active-low as available,
     * and `rng` is used for randomized page allocation.
     * */
    free_bitvec_t free_page_frames_{};
    std::mt19937_64 rng{0};
public:
    using ptwc_init_list_t = PageTableWalker::ptwc_init_list_t;
        
    OS(ptwc_init_list_t);

    uint64_t warmup_translate_ip(uint8_t coreid, uint64_t byteaddr);
    uint64_t warmup_translate_ldst(uint8_t coreid, uint64_t lineaddr);

    void tick(void);

    bool translate_ip(uint8_t coreid, iptr_t);
    bool translate_ldst(uint8_t coreid, iptr_t, uint64_t);
    /*
     * Searches for a free, random page frame. If found, then returns this pfn.
     * Otherwise, prints to `stderr` and exits with code 1.
     * */
    uint64_t get_and_reserve_free_page_frame(void);

    void print_stats(std::ostream&);
    /*
     * Inlines:
     * */
    inline void handle_l1d_outgoing(uint8_t coreid, const Transaction& t)
    {
        ptw_[coreid]->handle_l1d_outgoing(t);
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // OS_h

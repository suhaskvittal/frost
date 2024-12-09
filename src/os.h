/* author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef OS_h
#define OS_h

#include "constants.h"
#include "core/instruction.h"
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

class ITLB;
class DTLB;
class L2LTB;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class TranslationState { IN_TLB, IN_PTW, DONE };

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class OS
{
public:
    using itlb_ptr  = std::unique_ptr<ITLB>;
    using dtlb_ptr  = std::unique_ptr<DTLB>;
    using l2tlb_ptr = std::unique_ptr<L2TLB>;

    itlb_ptr  ITLB_;
    dtlb_ptr  DTLB_;
    l2tlb_ptr L2TLB_;

    uint64_t s_page_faults_ =0;
private:
    constexpr static size_t BITVEC_WIDTH = NUM_PAGE_FRAMES/64;

    struct CoreMMU
    {
        using vmem_ptr = std::unique_ptr<VirtualMemory>;
        using ptw_ptr = std::unique_ptr<PageTableWalker>;

        vmem_ptr vmem;
        ptw_ptr  ptw;

        CoreMMU(uint8_t coreid);
    };

    using mmu_ptr = std::unique_ptr<CoreMMU>;
    using mmu_array_t = std::array<mmu_ptr, NUM_THREADS>;
    using pending_itrans_t = std::unordered_map<iptr_t, TranslationState>;
    /*
     * Each core has its own virtual memory and page walker (`CoreMMU`):
     * */
    mmu_array_t core_mmu_;
    /*
     * Translation management:
     * */
    pending_itrans_t pending_ip_translations_;
    /*
     * Page frame management. `free_page_frames_` uses active-low as available,
     * and `rng` is used for randomized page allocation.
     * */
    free_bitvec_t free_page_frames_{};
    std::mt19937_64 rng{0};
public:
    OS(void) =default;

    void print_stats(std::ostream&);

    void tick(void);

    bool translate_ip(iptr_t);
    bool check_if_ip_translation_is_done(iptr_t);
private:
    uint64_t get_and_reserve_free_page_frame(void);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // OS_h

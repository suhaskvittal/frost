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

class ITLB;
class DTLB;
class L2LTB;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct PageTableEntry
{
    bool is_directory;
    /*
     * If `is_directory`, then `pfn` corresponds to the page frame of the
     * next-level table/directory. Otherwise, it is the page frame of the
     * desired virtual page.
     * */
    uint64_t pfn;

    PageTableEntry(bool dir, uint64_t pfn)
        :is_directory(dir),
        pfn(pfn)
    {}
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class TranslationState { IN_TLB, IN_PTW, DONE };

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class OS
{
public:
    constexpr static uint64_t PAGE_TABLE_BASE_REGISTER = 0;
    constexpr static size_t NUM_PAGE_FRAMES = (DRAM_SIZE_MB*1024*1024) / PAGESIZE;
    constexpr static size_t NUM_PTE_PER_TABLE = PAGESIZE/PTESIZE;

    using itlb_ptr  = std::unique_ptr<ITLB>;
    using dtlb_ptr  = std::unique_ptr<DTLB>;
    using l2tlb_ptr = std::unique_ptr<L2TLB>;

    itlb_ptr  ITLB_;
    dtlb_ptr  DTLB_;
    l2tlb_ptr L2TLB_;

    uint64_t s_page_faults_ =0;
private:
    constexpr static size_t BITVEC_WIDTH = NUM_PAGE_FRAMES/64;

    using pte_ptr = std::unique_ptr<PageTableEntry>;
    using page_table_t = std::array<pte_ptr, NUM_PTE_PER_PAGE>;
    using free_bitvec_t = std::array<uint64_t, BITVEC_WIDTH>;

    using pending_itrans_t = std::unordered_map<iptr_t, TranslationState>;

    page_table_t base_pt_{};
    /*
     * `free_page_frames_` uses active-low as available.
     * */
    free_bitvec_t free_page_frames_{};
    /*
     * Translation management
     * */
    pending_itrans_t pending_ip_translations_;

    /*
     * `rng` is for page frame allocation.
     * */
    std::mt19937_64 rng{0};
public:
    OS(void) =default;

    void print_stats(std::ostream&);

    void tick(void);

    bool translate_ip(iptr_t);
    bool check_if_ip_translation_is_done(iptr_t);
private:
    uint64_t get_pfn(uint64_t);
    void handle_page_fault(uint64_t vpn);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // OS_h

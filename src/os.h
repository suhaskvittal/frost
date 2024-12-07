/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef OS_h
#define OS_h

#include "constants.h"
#include "util/numerics.h"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <random>
#include <unordered_map>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class OS
{
public:
    constexpr static size_t NUM_PAGE_FRAMES = (DRAM_SIZE_MB*1024*1024) / PAGESIZE;

    uint64_t s_page_faults_ =0;
private:
    constexpr static size_t BITVEC_WIDTH = NUM_PAGE_FRAMES/64;

    using page_table_t = std::unordered_map<uint64_t, uint64_t>; 
    using free_bitvec_t = std::array<uint64_t, BITVEC_WIDTH>;

    page_table_t pt_;
    /*
     * `free_page_frames_` uses active-low as available.
     * */
    free_bitvec_t free_page_frames_{};
    /*
     * `rng` is for page frame allocation.
     * */
    std::mt19937_64 rng{0};
public:
    OS(void) =default;

    void print_stats(std::ostream&);

    uint64_t translate_byteaddr(uint64_t);
    uint64_t translate_lineaddr(uint64_t);
private:
    uint64_t get_pfn(uint64_t);
    void handle_page_fault(uint64_t vpn);
    /*
     * A generic function for translation.
     * */
    template <size_t N_PER_PAGE>
    uint64_t translate_addr(uint64_t addr)
    {
        uint64_t vpn = addr >> numeric_traits<N_PER_PAGE>::log2,
                 off = fast_mod<N_PER_PAGE>(addr);
        return (get_pfn(vpn) << numeric_traits<N_PER_PAGE>::log2) | off;
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // OS_h

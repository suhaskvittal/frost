/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "os.h"
#include "util/stats.h"

#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
OS::print_stats(std::ostream& out)
{
    out << BAR << "\n";
    print_stat(out, "OS", "PAGE_FAULTS", s_page_faults_);
    out << BAR << "\n";
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

uint64_t
OS::translate_byteaddr(uint64_t addr)
{
    return translate_addr<PAGESIZE>(addr);
}

uint64_t
OS::translate_lineaddr(uint64_t addr)
{
    return translate_addr<PAGESIZE/LINESIZE>(addr);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

uint64_t
OS::get_pfn(uint64_t vpn)
{
    if (!pt_.count(vpn))
        handle_page_fault(vpn);
    return pt_[vpn];
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
OS::handle_page_fault(uint64_t vpn)
{
    ++s_page_faults_;
    for (size_t i = 0; i < 1024; i++) {
        size_t pfn = numeric_traits<NUM_PAGE_FRAMES>::mod(rng());
        size_t ii = pfn >> 6,
               jj = pfn & 0x3f;
        bool is_taken = free_page_frames_[ii] & (1L << jj);
        if (!is_taken) {
            free_page_frames_[ii] |= (1L << jj);
            pt_[vpn] = pfn;
            return;
        }
    }
    size_t num_free = std::accumulate(free_page_frames_.begin(),
                                        free_page_frames_.end(),
                                        static_cast<size_t>(0),
                                        [] (uint64_t x, uint64_t y)
                                        {
                                            return x + __builtin_popcount(~y);
                                        });
    std::cerr << "os: failed to find free page frame, free page frames: " << num_free << "\n";
    exit(1);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

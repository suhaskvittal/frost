/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "os.h"

#include <iostream>

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
    for (size_t i = 0; i < 1024; i++) {
        size_t pfn = numeric_traits<NUM_PAGE_FRAMES>::mod(rng());
        size_t ii = pfn >> numeric_traits<BITVEC_WIDTH>::log2,
               jj = numeric_traits<BITVEC_WIDTH>::mod(pfn);
        bool is_taken = free_page_frames_[ii] & (1L << jj);
        if (!is_taken) {
            free_page_frames_[ii] |= (1L << jj);
            pt_[vpn] = pfn;
            return;
        }
    }
    std::cerr << "os: failed to find free page frame.\n";
    exit(1);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

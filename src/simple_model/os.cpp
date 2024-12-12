/*
 *  author: Suhas Vittal
 *  date:   12 December 2024
 * */

#include "os/address.h"
#include "simple_model/os.h"
#include "util/stats.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

constexpr size_t TAG_OFFSET = 48;

uint64_t
OS::translate_lineaddr(uint64_t lineaddr, uint8_t coreid)
{
    // First, tag the line address with the coreid.
    lineaddr |= static_cast<uint64_t>(coreid) << TAG_OFFSET;

    auto [vpn, offset] = split_address<LINESIZE>(lineaddr);
    if (!pt_.count(vpn))
        pt_.insert({vpn, free_list_.get_and_reserve_free_page_frame()});
    uint64_t pfn = pt_.at(vpn);
    return join_address<LINESIZE>(pfn, offset);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
OS::print_stats(std::ostream& out)
{
    out << BAR << "\n";
    print_stat(out, "OS", "PAGE_FAULTS", free_list_.s_page_faults_);
    out << BAR << "\n";
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

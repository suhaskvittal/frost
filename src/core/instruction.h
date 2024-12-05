/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef CORE_INSTRUCTION_h
#define CORE_INSTRUCTION_h

#include "trace/data.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct Instruction
{
    /*
     * These are set upon instantiation.
     * */
    uint64_t   ip;
    bool       branch_taken;
    BranchType branch_type;

    std::vector<uint64_t> loads;
    std::vector<uint64_t> stores;
    /*
     * Additional metadata that can be used:
     *  (1) `ip_translated`, `pip`, and `inst_data_avail` are all for management
     *      iTLB and L1i$ interactions.
     *  (2) `p_ld_addr` and `p_st_addr` are physical addresses used for the L1D$.
     *  (3) `cycle_xxx` is metadata for stats.
     * */
    bool ip_translated =false;
    uint64_t pip;
    bool inst_data_avail =false;

    std::vector<uint64_t> p_ld_addr;
    std::vector<uint64_t> p_st_addr;

    uint64_t cycle_fetched = std::numeric_limits<uint64_t>::max();
    uint64_t cycle_issued = std::numeric_limits<uint64_t>::max();
    uint64_t cycle_done = std::numeric_limits<uint64_t>::max();

    Instruction(TraceData d)
        :ip(d.ip),
        branch_taken(d.branch_taken),
        branch_type(d.branch_type),
        loads(d.src_mem),
        stores(d.dst_mem)
    {}

    inline bool is_mem_inst(void)
    {
        return !loads.empty() || !stores.empty();
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CORE_INSTRUCTION_h

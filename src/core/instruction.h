/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef CORE_INSTRUCTION_h
#define CORE_INSTRUCTION_h

#include "trace/data.h"

#include <vector>
#include <unordered_set>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct Instruction
{
    using vmemop_list_t = std::vector<uint64_t>;
    using pmemop_set_t = std::unordered_set<uint64_t>;
    /*
     * These are set upon instantiation.
     * */
    uint64_t   ip;
    bool       branch_taken;
    BranchType branch_type;

    vmemop_list_t loads;
    vmemop_list_t stores;
    /*
     * Additional metadata that can be used for
     * managing interactions with i-caches and d-caches.
     * */
    bool ip_translated =false;
    uint64_t pip;
    bool inst_data_avail =false;

    pmemop_set_t p_ld_lineaddr;
    pmemop_set_t p_st_lineaddr;
    bool awaiting_loads =false;

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

    inline bool is_mem_inst(void) const
    {
        return !loads.empty() || !stores.empty();
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CORE_INSTRUCTION_h

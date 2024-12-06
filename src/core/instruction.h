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
     * Additional metadata for iTLB and L1i interaction
     * */
    bool ready_for_icache_access =false;
    bool inst_load_done =false;
    uint64_t pip;
    /*
     * Additional metadata for dTLB and L1d interaction
     * */
    pmemop_set_t v_ld_lineaddr;
    pmemop_set_t v_st_lineaddr;
    pmemop_set_t p_ld_lineaddr;
    pmemop_set_t p_st_lineaddr;
    uint64_t loads_in_progress =0;
    /*
     * Additional metadata for stats and ROB management
     * */
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

    inline bool mem_inst_is_done(void) const
    {
        return v_ld_lineaddr.empty() && p_ld_lineaddr.empty()
                && v_st_lineaddr.empty() && p_st_lineaddr.empty()
                && loads_in_progress == 0;
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CORE_INSTRUCTION_h

/*
 *  author: Suhas Vittal
 *  date:   9 December 2024
 * */

#include "constants.h"

#include "instruction.h"
#include "trace/data.h"
#include "util/numerics.h"

#include <algorithm>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline void
xform_into_memop(const std::vector<uint64_t>& orig, Instruction::memop_list_t& memops)
{
    std::transform(orig.cbegin(), orig.cend(), std::back_inserter(memops),
            [] (uint64_t addr)
            { 
                return Memop(addr >> numeric_traits<LINESIZE>::log2);
            });
}

Instruction::Instruction(uint64_t inum, TraceData d)
    :inst_num(inum),
    ip(d.ip),
    branch_taken(d.branch_taken),
    branch_type(d.branch_type)
{
    xform_into_memop(d.src_mem, loads);
    xform_into_memop(d.dst_mem, stores);

    num_loads_in_state[static_cast<int>(AccessState::NOT_READY)] = loads.size();
    num_stores_in_state[static_cast<int>(AccessState::NOT_READY)] = stores.size();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
Instruction::is_mem_inst() const
{
    return !loads.empty() || !stores.empty();
}

bool
Instruction::is_done() const
{
    const size_t i = static_cast<size_t>(AccessState::DONE),
                 j = static_cast<size_t>(AccessState::IN_CACHE);
    return num_loads_in_state.at(i) == loads.size() && num_stores_in_state.at(j) == stores.size();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/*
 *  author: Suhas Vittal
 *  date:   9 December 2024
 * */

#include "constants.h"
#include "globals.h"
#include "memsys.h"

#include "core/instruction.h"
#include "os/address.h"
#include "os.h"
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

template <AccessState STATE, class FUNC> void
do_func_dependent_on_state(Instruction::memop_state_array_t& st, Instruction::memop_list_t& v, const FUNC& func)
{
    constexpr size_t N = static_cast<size_t>(STATE);
    if (st[N] < v.size()) {
        func(v);
        st[N] = std::count_if(v.begin(), v.end(),
                        [] (const Memop& x)
                        {
                            return static_cast<size_t>(x.state) >= N;
                        });
    }
}

template <AccessState STATE, class FUNC> void
do_func_dependent_on_state(iptr_t& inst, const FUNC& func)
{
    do_func_dependent_on_state<STATE, FUNC>(inst->num_loads_in_state, inst->loads, func);
    do_func_dependent_on_state<STATE, FUNC>(inst->num_stores_in_state, inst->stores, func);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
inst_dtlb_access(iptr_t& inst, uint8_t coreid)
{
    auto func = [inst, coreid] (Instruction::memop_list_t& v)
    {
        for (Memop& x : v) {
            if (x.state == AccessState::NOT_READY && GL_OS->translate_ldst(coreid, inst, x.v_lineaddr))
                x.state = AccessState::IN_TLB;
        }
    };
    
    do_func_dependent_on_state<AccessState::IN_TLB>(inst, func);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline void
update_memops_post_translate(Instruction::memop_list_t& v, uint64_t match_vpn, uint64_t pfn)
{
}

void
inst_dtlb_done(iptr_t& inst, uint64_t vpn, uint64_t pfn)
{
    auto func = [match_vpn=vpn, pfn] (Instruction::memop_list_t& v)
    {
        for (Memop& x : v) {
            if (x.state == AccessState::IN_TLB) {
                const auto [vpn, offset] = split_address<LINESIZE>(x.v_lineaddr);
                x.p_lineaddr = join_address<LINESIZE>(pfn, offset);
                x.state = AccessState::READY;
            }
        }
    };

    do_func_dependent_on_state<AccessState::READY>(inst, func);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <TransactionType TTYPE> inline void
do_ldst(iptr_t& inst,
        uint8_t coreid,
        std::unique_ptr<L1DCache>& cache,
        Instruction::memop_state_array_t& st,
        Instruction::memop_list_t& v)
{
    do_func_dependent_on_state<AccessState::IN_CACHE>(st, v, 
            [inst, coreid, c=cache.get()] (Instruction::memop_list_t& v)
            {
                for (Memop& x : v) {
                    if (x.state == AccessState::READY) {
                        Transaction trans(coreid, inst, TTYPE, x.p_lineaddr);
                        if (c->io_->add_incoming(trans))
                            x.state = AccessState::IN_CACHE;
                    }
                }
            });
}

void
inst_dcache_access(iptr_t& inst, uint8_t coreid, std::unique_ptr<L1DCache>& cache)
{
    do_ldst<TransactionType::READ>(inst, coreid, cache, inst->num_loads_in_state, inst->loads);
    do_ldst<TransactionType::WRITE>(inst, coreid, cache, inst->num_stores_in_state, inst->stores);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

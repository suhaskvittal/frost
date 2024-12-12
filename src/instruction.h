/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef INSTRUCTION_h
#define INSTRUCTION_h

#include "branch.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class TraceData;
class L1DCache;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class AccessState {
    NOT_READY =0,
    IN_TLB    =1,
    READY     =2,
    IN_CACHE  =3,
    DONE      =4,
    SIZE      =5
};

struct Memop
{
    uint64_t v_lineaddr;
    uint64_t p_lineaddr;
    AccessState state =AccessState::NOT_READY;

    Memop(uint64_t addr)
        :v_lineaddr(addr)
    {}
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct Instruction
{
    using memop_list_t = std::vector<Memop>;
    using memop_state_array_t = std::array<size_t, static_cast<int>(AccessState::SIZE)>;
    /*
     * These are set upon instantiation.
     * */
    uint64_t   inst_num;
    uint64_t   ip;
    bool       branch_taken;
    BranchType branch_type;
    /*
     * Additional metadata for iTLB and L1i
     * */
    AccessState ip_state =AccessState::NOT_READY;
    uint64_t    pip;  // This is a byte address.
    /*
     * Additional metadata for dTLB and L1d
     *
     * Continually accessing `loads` and `stores` to
     * check this instruction's progress is slow.
     * We track the number of loads and stores in
     * each stage of the access instead.
     * */
    memop_list_t loads;
    memop_list_t stores;

    memop_state_array_t num_loads_in_state{};
    memop_state_array_t num_stores_in_state{};
    /*
     * Additional metadata for stats and ROB management
     * */
    uint64_t cycle_fetched = std::numeric_limits<uint64_t>::max();
    uint64_t cycle_issued = std::numeric_limits<uint64_t>::max();
    uint64_t cycle_rob_head = std::numeric_limits<uint64_t>::max();
    uint64_t cycle_done = std::numeric_limits<uint64_t>::max();
    bool retired =false;

    Instruction(uint64_t inum, TraceData);

    bool is_mem_inst(void) const;
    bool is_done(void) const;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

using iptr_t = std::shared_ptr<Instruction>;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * These functions here are very useful for implementing functions with
 * state updates, such as cache accesses (on success -- want to indicate
 * that the access is unneeded).
 *
 * As shown, the function given only executes if any entry is not
 * in `STATE`.
 * */
template <AccessState STATE, class FUNC> void
inst_do_func_dependent_on_state(Instruction::memop_state_array_t& st, Instruction::memop_list_t& v, const FUNC& func)
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
inst_do_func_dependent_on_state(iptr_t& inst, const FUNC& func)
{
    inst_do_func_dependent_on_state<STATE, FUNC>(inst->num_loads_in_state, inst->loads, func);
    inst_do_func_dependent_on_state<STATE, FUNC>(inst->num_stores_in_state, inst->stores, func);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CORE_INSTRUCTION_h

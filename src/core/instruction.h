/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef CORE_INSTRUCTION_h
#define CORE_INSTRUCTION_h

#include "core/branch.h"

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

void inst_dtlb_access(iptr_t&, uint8_t coreid);
void inst_dtlb_done(iptr_t&, uint64_t vpn, uint64_t pfn);
void inst_dcache_access(iptr_t&, uint8_t coreid, std::unique_ptr<L1DCache>&);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // CORE_INSTRUCTION_h

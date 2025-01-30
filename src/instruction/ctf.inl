/*
 *  author: Suhas Vittal
 *  date:   28 January 2025
 * */

#include "branch.h"
#include "util/numerics.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct Memop
{
    uint64_t vla;
    uint64_t pla;
    AccessState state =AccessState::NOT_READY;

    inline Memop(uint64_t addr)
        :vla(addr)
    {}
};

struct MemopList
{
    using memop_list_type = std::vector<Memop>;
    using memop_state_array_type = std::array<size_t, static_cast<int>(AccessState::SIZE)>;

    memop_list_type        args;
    memop_state_array_type num_in_state{};
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct Instruction : INST_BASE
{

    uint64_t   ip;
    bool       branch_taken;
    BranchType branch_type;
    /*
     * Metadata for iTLB and L1i$
     * */
    AccessState ip_state =AccessState::NOT_READY;
    uint64_t   pip;  // This is a byte address, just like `ip`.
    /*
     * Metadata for dTLB and L1d$
     * */                 
    MemopList loads;
    MemopList stores;

    Instruction(uint64_t inst_num, const CTF&);

    inline bool is_mem_inst(void) const override
    {
        return !loads.args.empty() || !stores.args.empty();
    }

    inline bool is_done(void) const override
    {
        constexpr size_t DONE_IDX = static_cast<size_t>(AccessState::DONE);
        constexpr size_t IN_CACHE_IDX = static_cast<size_t>(AccessState::IN_CACHE);

        return loads.num_in_state.at(DONE_IDX) == loads.args.size()
                    && stores.num_in_state.at(IN_CACHE_IDX) == stores.args.size();
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline
Instruction::Instruction(uint64_t ii, const CTF& t)
{
    inst_num = ii;
    ip = t.ip;
    branch_taken = t.branch_taken;

    std::vector<uint64_t> dst_regs,
                          src_regs;
    // First resolve branch data.
    std::remove_copy(std::begin(t.dst_regs), std::end(t.dst_regs), std::back_inserter(dst_regs), 0);
    std::remove_copy(std::begin(t.src_regs), std::end(t.src_regs), std::back_inserter(src_regs), 0);

    bool reads_sp = false,
         reads_ip = false,
         reads_cc = false,
         reads_other = false,
         writes_sp = false,
         writes_ip = false;

    for (auto x : src_regs)
    {
        reads_sp |= x == CTF::R_SP;
        reads_ip |= x == CTF::R_IP;
        reads_cc |= x == CTF::R_CC;
        reads_other |= (x != CTF::R_SP
                        && x != CTF::R_IP
                        && x != CTF::R_CC);
    }

    for (auto x : dst_regs)
    {
        writes_sp |= x == CTF::R_SP;
        writes_ip |= x == CTF::R_IP;
    }

    if (writes_ip)
    {
        // Then this is definitely some sort of branch.
        if (!reads_cc && !reads_sp && !writes_sp)
            branch_type = reads_other ? BranchType::INDIRECT : BranchType::DIRECT;
        else if (reads_cc && !reads_other && !reads_sp && !writes_sp)
            branch_type = BranchType::CONDITIONAL;
        else if (reads_sp && writes_sp && !reads_cc)
            branch_type = reads_other ? BranchType::INDIRECT_CALL : BranchType::DIRECT_CALL;
        else if (!reads_ip && reads_sp && writes_sp)
            branch_type = BranchType::RETURN;
        else
        {
            std::cerr << "instruction: unknown branch found"
                << "\n\tsource registers:";
            for (uint64_t r : src_regs)
                std::cerr << " " << r;
            std::cerr << "\n\tdestination registers:";
            for (uint64_t r : dst_regs)
                std::cerr << " " << r;
            std::cerr << "\n\treads_sp: " << reads_sp
                << "\n\treads_ip: " << reads_ip
                << "\n\treads_cc: " << reads_cc
                << "\n\treads_other: " << reads_other
                << "\n\twrites_sp: " << writes_sp
                << "\n\twrites_ip: " << writes_ip;
            exit(1);
        }
    } 
    else
        branch_type = BranchType::INVALID;

    // Now resolve load/store data.
    constexpr size_t LINESIZE = 64;
    for (uint64_t x : t.dst_mem)
    {
        if (x != 0)
            stores.args.emplace_back(x >> numeric_traits<LINESIZE>::log2);
    }
    
    for (uint64_t x : t.src_mem)
    {
        if (x != 0)
            stores.args.emplace_back(x >> numeric_traits<LINESIZE>::log2);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <AccessState STATE, class FUNC> inline void
inst_do_func_dependent_on_state(MemopList& m, const FUNC& func)
{
    constexpr size_t N = static_cast<size_t>(STATE);
    if (m.num_in_state[N] < m.args.size())
    {
        func(m.args);
        m.num_in_state[N] = std::count_if(m.args.begin(), m.args.end(),
                                [] (const Memop& x)
                                {
                                    return static_cast<size_t>(x.state) >= N;
                                });
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

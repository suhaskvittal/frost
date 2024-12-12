/*
 *  author: Suhas Vittal
 *  date:   9 December 2024
 * */

#include "constants.h"

#include "instruction.h"
#include "trace/fmt.h"
#include "util/numerics.h"

#include <cstring>
#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

Instruction::Instruction(uint64_t i)
    :inst_num(i)
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

Instruction::Instruction(uint64_t inst_num, const ChampsimTraceFormat& blk)
    :inst_num(inst_num),
    ip(blk.ip),
    branch_taken(blk.branch_taken)
{
    std::vector<uint64_t> dst_regs,
                          src_regs;
    // First resolve branch data.
    std::remove_copy(std::begin(blk.dst_regs), std::end(blk.dst_regs), std::back_inserter(dst_regs), 0);
    std::remove_copy(std::begin(blk.src_regs), std::end(blk.src_regs), std::back_inserter(src_regs), 0);

    bool reads_sp = false,
         reads_ip = false,
         reads_cc = false,
         reads_other = false,
         writes_sp = false,
         writes_ip = false;
    for (auto x : src_regs) {
        reads_sp |= x == ChampsimTraceFormat::R_SP;
        reads_ip |= x == ChampsimTraceFormat::R_IP;
        reads_cc |= x == ChampsimTraceFormat::R_CC;
        reads_other |= (x != ChampsimTraceFormat::R_SP
                        && x != ChampsimTraceFormat::R_IP
                        && x != ChampsimTraceFormat::R_CC);
    }
    for (auto x : dst_regs) {
        writes_sp |= x == ChampsimTraceFormat::R_SP;
        writes_ip |= x == ChampsimTraceFormat::R_IP;
    }

    if (writes_ip) {
        // Then this is definitely some sort of branch.
        if (!reads_cc && !reads_sp && !writes_sp)
            branch_type = reads_other ? BranchType::INDIRECT : BranchType::DIRECT;
        else if (reads_cc && !reads_other && !reads_sp && !writes_sp)
            branch_type = BranchType::CONDITIONAL;
        else if (reads_sp && writes_sp && !reads_cc)
            branch_type = reads_other ? BranchType::INDIRECT_CALL : BranchType::DIRECT_CALL;
        else if (!reads_ip && reads_sp && writes_sp)
            branch_type = BranchType::RETURN;
        else {
            std::cerr << "instruction: unknown branch found"
                << "\n\tsource registers:";
            for (uint8_t r : src_regs)
                std::cerr << " " << r+0;
            std::cerr << "\n\tdestination registers:";
            for (uint8_t r : dst_regs)
                std::cerr << " " << r+0;
            std::cerr << "\n\treads_sp: " << reads_sp
                << "\n\treads_ip: " << reads_ip
                << "\n\treads_cc: " << reads_cc
                << "\n\treads_other: " << reads_other
                << "\n\twrites_sp: " << writes_sp
                << "\n\twrites_ip: " << writes_ip;
            exit(1);
        }
    } else {
        branch_type = BranchType::INVALID;
    }
    // Now resolve load/store data.
    for (uint64_t x : blk.dst_mem)
        if (x)
            stores.emplace_back(x >> numeric_traits<LINESIZE>::log2);
    for (uint64_t x : blk.src_mem)
        if (x)
            loads.emplace_back(x >> numeric_traits<LINESIZE>::log2);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

Instruction::Instruction(const MemsimTraceFormat& blk)
    :inst_num(0)
{
    uint64_t addr{};
    memcpy(&inst_num, blk.inst_num, sizeof(blk.inst_num));
    memcpy(&addr, blk.v_lineaddr, sizeof(blk.v_lineaddr));
    if (blk.is_write)
        stores = {addr};
    else
        loads = {addr};
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

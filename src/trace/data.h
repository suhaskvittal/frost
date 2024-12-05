/*
 *  author: Suhas Vittal
 *  date:   27 November 2024
 * */

#ifndef TRACE_DATA_h
#define TRACE_DATA_h

#include "core/branch.h"

#include <cstdint>
#include <cstddef>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct ChampSimTraceFormat {
    constexpr static uint8_t R_SP = 6;
    constexpr static uint8_t R_CC = 25;
    constexpr static uint8_t R_IP = 26;

    constexpr static size_t NUM_DST = 2;
    constexpr static size_t NUM_SRC = 4;

    uint64_t ip;
    uint8_t is_branch;
    uint8_t branch_taken;
    uint8_t dst_regs[NUM_DST];
    uint8_t src_regs[NUM_SRC];
    uint64_t dst_mem[NUM_DST];
    uint64_t src_mem[NUM_SRC];
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct TraceData {
    uint64_t ip;
    bool branch_taken;
    BranchType branch_type =BranchType::INVALID;

    std::vector<uint8_t> dst_regs;
    std::vector<uint8_t> src_regs;
    std::vector<uint64_t> dst_mem;
    std::vector<uint64_t> src_mem;
        
    bool reads_sp =false;
    bool writes_sp =false;
    bool reads_ip =false;
    bool writes_ip =false;
    bool reads_cc =false;
    bool reads_other =false;

    TraceData(const ChampSimTraceFormat&);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif  // TRACE_DATA_h


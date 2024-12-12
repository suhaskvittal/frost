/*
 *  author: Suhas Vittal
 *  date:   27 November 2024
 * */

#ifndef TRACE_FMT_h
#define TRACE_FMT_h

#include <cstdint>
#include <cstddef>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct ChampsimTraceFormat
{
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

struct MemsimTraceFormat
{
    char inst_num[5];
    char is_write;
    char v_lineaddr[4];
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif  // TRACE_FMT_h

/*
 *  author: Suhas Vittal
 *  date:   27 November 2024
 * */

#include "trace/data.h"

#include <algorithm>
#include <iostream>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> inline void
move_all_nonzero(std::vector<T>& to, const T* from, size_t size)
{
    to.reserve(size);
    for (size_t i = 0; i < size; i++) {
        if (from[i] != 0) 
            to.push_back(from[i]);
    }
}

template <class T, T MATCH> inline bool
any_of(std::vector<T>& arr)
{
    return std::any_of(arr.begin(), arr.begin(),
                    [] (const T& x)
                    {
                        return x == MATCH;
                    });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TraceData::TraceData(const ChampSimTraceFormat& d)
    :ip(d.ip),
    branch_taken(d.branch_taken)
{
    move_all_nonzero(dst_regs, d.dst_regs, ChampSimTraceFormat::NUM_DST);
    move_all_nonzero(src_regs, d.src_regs, ChampSimTraceFormat::NUM_SRC);
    move_all_nonzero(dst_mem, d.dst_mem, ChampSimTraceFormat::NUM_DST);
    move_all_nonzero(src_mem, d.src_mem, ChampSimTraceFormat::NUM_SRC);

    reads_sp = any_of<uint8_t, ChampSimTraceFormat::R_SP>(src_regs);
    reads_ip = any_of<uint8_t, ChampSimTraceFormat::R_SP>(src_regs);
    reads_cc = any_of<uint8_t, ChampSimTraceFormat::R_SP>(src_regs);

    reads_other = std::any_of(src_regs.begin(), src_regs.end(),
                        [] (uint8_t r)
                        {
                            return r != ChampSimTraceFormat::R_SP
                                    && r != ChampSimTraceFormat::R_IP
                                    && r != ChampSimTraceFormat::R_CC;
                        });

    writes_sp = any_of<uint8_t, ChampSimTraceFormat::R_SP>(dst_regs);
    writes_ip = any_of<uint8_t, ChampSimTraceFormat::R_IP>(dst_regs);

    if (writes_ip && !reads_cc && !reads_sp && !writes_sp) {
        branch_type = reads_other ? BranchType::INDIRECT : BranchType::DIRECT;
    } else if (writes_ip && reads_cc && !reads_other && !reads_sp && !writes_sp) {
        branch_type = BranchType::CONDITIONAL;
    } else if (writes_ip && reads_sp && writes_sp && !reads_cc) {
        branch_type = reads_other ? BranchType::INDIRECT_CALL : BranchType::DIRECT_CALL;
    } else if (!reads_ip && writes_ip && reads_sp && writes_sp) {
        branch_type = BranchType::RETURN;
    } else if (writes_ip) {
        std::cerr << "Unknown branch found!\n";
        exit(1);
    } else {
        branch_type = BranchType::INVALID;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

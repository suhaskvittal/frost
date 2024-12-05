/*
 *  author: Suhas Vittal
 *  date:   27 November 2024
 * */

#include "trace_data.h"

#include <algorithm>

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
                    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TraceData::TraceData(const ChampSimTraceFormat& d)
    :ip_(d.ip),
    branch_taken_(d.branch_taken)
{
    move_all_nonzero(dst_regs, d.dst_regs, ChampSimTraceFormat::NUM_DST);
    move_all_nonzero(src_regs, d.src_regs, ChampSimTraceFormat::NUM_SRC);
    move_all_nonzero(dst_mem, d.dst_mem, ChampSimTraceFormat::NUM_DST);
    move_all_nonzero(src_mem, d.src_mem, ChampSimTraceFormat::NUM_SRC);

    reads_sp = any_of<uint8_t, ChampSimTraceFormat::R_SP>(src_regs);
    reads_ip = any_of<uint8_t, ChampSimTraceFormat::R_SP>(src_regs);
    reads_cc = any_of<uint8_t, ChampSimTraceFormat::R_SP>(src_regs);

    reads_other = std::any_of(src_regs.begin(), src_regs.end()
                        [] (uint8_t r)
                        {
                            return r != ChampSimTraceFormat::R_SP
                                    && r != ChampSimTraceFormat::R_IP
                                    && r != ChampSimTraceFormat::R_CC;
                        });

    writes_sp = any_of<uint8_t, ChampSimTraceFormat::R_SP>(dst_regs);
    writes_ip = any_of<uint8_t, ChampSimTraceFormat::R_IP>(dst_regs);

    if (writes_ip_ && !reads_cc_ && !reads_sp_ && !writes_sp_) {
        branch_type = reads_other_ ? BranchType::INDIRECT : BranchType::DIRECT;
    } else if (writes_ip_ && reads_cc_ && !reads_other_ && !reads_sp_ && !writes_sp_) {
        branch_type = BranchType::CONDITIONAL;
    } else if (writes_ip_ && reads_sp_ && writes_sp_ && !reads_cc_) {
        branch_type = reads_other_ ? BranchType::INDIRECT_CALL : BranchType::DIRECT_CALL;
    } else if (!reads_ip_ && writes_ip_ && reads_sp_ && writes_sp_) {
        branch_type = BranchType::RETURN;
    } else if (writes_ip_) {
        std::cerr << "Unknown branch found:\n" << *this << "\n";
        exit(1);
    } else {
        branch_type = BranchType::INVALID;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::string
read_write_or_none(bool read, bool write)
{
    if (read && write) return "RW";
    else if (read) return "R";
    else if (write) return "W";
    else return "";
}

std::ostream&
operator<<(std::ostream& out, const TraceData& d)
{
    out << "INSTRUCTION {\n"
        << "\tip = " << std::hex << d.ip << std::dec
        << "\tis branch = " << d.is_branch << ":" << (d.branch_taken ? "T" : "NT") 
            << "\ttype: " << d.branch_type << "\n"
        << "\tRegister Info ------\n"
        << "\t\tip: " << read_write_or_none(d.reads_ip, d.writes_ip) << "\n"
        << "\t\tsp: " << read_write_or_none(d.reads_sp, d.writes_sp) << "\n"
        << "\t\tcc: " << read_write_or_none(d.reads_cc, false) << "\n"
        << "\t\tother: " << read_write_or_none(d.reads_other, false) << "\n"
        << "\tMemory Info ------\n"
        << "\t\tnum loads: " << d.src_mem.size() << ", num stores: " << d.dst_mem.size() << "\n"
        << "}\n";
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

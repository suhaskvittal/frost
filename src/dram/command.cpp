/*
 *  author: Suhas Vittal
 *  date:   15 December 2024
 * */

#include "dram/address.h"
#include "dram/command.h"

#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMCommand::DRAMCommand()
    :type(DRAMCommandType::INVALID)
{}

DRAMCommand::DRAMCommand(uint64_t addr, DRAMCommandType t)
    :address(addr),
    type(t)
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

std::string
cmd_string(DRAMCommandType t)
{
    switch (t)
    {
    case DRAMCommandType::READ:
        return "READ";
    case DRAMCommandType::WRITE:
        return "WRITE";
    case DRAMCommandType::READ_PRECHARGE:
        return "READp";
    case DRAMCommandType::WRITE_PRECHARGE:
        return "WRITEp";
    case DRAMCommandType::ACTIVATE: 
        return "ACT";
    case DRAMCommandType::PRECHARGE:
        return "PRE";
    default:
        return "INVALID";
    }
}

std::ostream&
operator<<(std::ostream& out, const DRAMCommand& cmd)
{
    size_t ch = dram_channel(cmd.address),
           ra = dram_rank(cmd.address),
           bg = dram_bankgroup(cmd.address),
           ba = dram_bank(cmd.address),
           ro = dram_row(cmd.address);
    out << cmd_string(cmd.type) << "("
        << ch << "_"
        << ra << "_"
        << bg << "_"
        << ba << "_"
        << ro << ")";
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

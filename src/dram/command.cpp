/*
 *  author: Suhas Vittal
 *  date:   15 December 2024
 * */

#include "globals.h"

#include "dram/address.h"
#include "dram/command.h"

#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMCommand::DRAMCommand()
    :DRAMCommand(0, DRAMCommandType::INVALID)
{}

DRAMCommand::DRAMCommand(uint64_t addr, DRAMCommandType t)
    :DRAMCommand(Transaction(0, nullptr, TransactionType::READ, addr), t)
{}

DRAMCommand::DRAMCommand(Transaction trans, DRAMCommandType t)
    :trans(trans),
    type(t),
    cycle_entered_cmd_queue(GL_DRAM_CYCLE)
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
    size_t ch = dram_channel(cmd.trans.address),
           ra = dram_rank(cmd.trans.address),
           bg = dram_bankgroup(cmd.trans.address),
           ba = dram_bank(cmd.trans.address),
           ro = dram_row(cmd.trans.address);
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

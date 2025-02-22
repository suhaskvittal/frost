/*
 *  author: Suhas Vittal
 *  date:   15 December 2024
 * */

#include "dram/address.h"
#include "dram/command.h"

#include <iostream>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

std::string
cmd_string(const DRAMCommand& cmd)
{
    std::string base_string;

    if (cmd.type == DRAMCommand::Type::READ)
        base_string = "READ";
    else if (cmd.type == DRAMCommand::Type::WRITE)
        base_string = "WRITE";
    else if (cmd.type == DRAMCommand::Type::ACTIVATE)
        base_string = "ACT";
    else if (cmd.type == DRAMCommand::Type::PRECHARGE)
        base_string = "PRE";
    else
        base_string = "INV";

    if (cmd.autopre)
        base_string += "p";
    if (cmd.counter_update)
        base_string += "cu";

    base_string += "( " + std::to_string(dram_channel(cmd.address))
                + " | " + std::to_string(dram_bank_idx(cmd.address))
                + " | " + std::to_string(dram_row(cmd.address)) + " )";

    return base_string;
}

std::ostream&
operator<<(std::ostream& out, const DRAMCommand& cmd)
{
    out << cmd_string(cmd);
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

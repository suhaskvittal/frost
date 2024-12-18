/*
 *  author: Suhas Vittal
 *  date:   15 December 2024
 * */

#include "globals.h"

#include "dram/command.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMCommand::DRAMCommand()
    :DRAMCommand(0, DRAMCommandType::READ)
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

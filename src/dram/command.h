/*
 *  author: Suhas Vittal
 *  date:   15 December 2024
 * */

#ifndef DRAM_COMMAND_h
#define DRAM_COMMAND_h

#include "transaction.h"

#include <cstdint>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class DRAMCommandType {
    READ,
    READ_PRECHARGE,
    WRITE,
    WRITE_PRECHARGE,
    ACTIVATE,
    PRECHARGE,
    INVALID
};

inline bool cmd_is_invalid(DRAMCommandType t)
{
    return t == DRAMCommandType::INVALID;
}

inline bool cmd_is_read(DRAMCommandType t)
{
    return t == DRAMCommandType::READ || t == DRAMCommandType::READ_PRECHARGE;
}

inline bool cmd_is_write(DRAMCommandType t)
{
    return t == DRAMCommandType::WRITE || t == DRAMCommandType::WRITE_PRECHARGE;
}

inline bool cmd_is_cas(DRAMCommandType t)
{
    return cmd_is_read(t) || cmd_is_write(t);
}

inline bool cmd_is_autopre(DRAMCommandType t)
{
    return t == DRAMCommandType::READ_PRECHARGE || t == DRAMCommandType::WRITE_PRECHARGE;
}

inline bool cmd_is_act(DRAMCommandType t)
{
    return t == DRAMCommandType::ACTIVATE;
}

inline bool cmd_is_pre(DRAMCommandType t)
{
    return t == DRAMCommandType::READ_PRECHARGE
        || t == DRAMCommandType::WRITE_PRECHARGE
        || t == DRAMCommandType::PRECHARGE;
}

inline bool cmd_is_pre_only(DRAMCommandType t)
{
    return t == DRAMCommandType::PRECHARGE;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct DRAMCommand
{
    Transaction trans;
    DRAMCommandType type;
    bool is_row_buffer_hit =true;

    uint64_t cycle_entered_cmd_queue;

    DRAMCommand(void);
    DRAMCommand(uint64_t addr, DRAMCommandType);
    DRAMCommand(Transaction, DRAMCommandType);

    DRAMCommand(const DRAMCommand&) =default;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_COMMAND_h


/*
 *  author: Suhas Vittal
 *  date:   15 December 2024
 * */

#include "dram/address.h"
#include "dram/bank.h"

#include <algorithm>
#include <cstddef>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

sel_cmd_t
FCFS(cmdq_iterator cmd_it, DRAMBank& b)
{
    sel_cmd_t out;
    uint64_t addr = cmd_it->trans.address;
    size_t r = dram_row(addr);
    if (b.open_row_.has_value()) {
        if (r == b.open_row_)
            out = *cmd_it;
        else
            out = DRAMCommand(addr, DRAMCommandType::PRECHARGE);
    } else {
        out = DRAMCommand(addr, DRAMCommandType::ACTIVATE);
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

sel_cmd_t
FRFCFS(cmdq_iterator cmd_it, DRAMBank& b)
{
    sel_cmd_t out;
    // Get command that must be done to perform the R/W
    uint64_t addr = cmd_it->trans.address;
    size_t r = dram_row(addr);
    if (b.open_row_.has_value()) {
        if (r == b.open_row_) {
            out = *cmd_it;
        } else {
            // Row buffer miss: check precharge conditions.
            if (cmd_it != b.cmd_queue_.begin())
                return out;
            bool any_pending_hits = std::any_of(std::next(cmd_it), b.cmd_queue_.end(),
                                            [b] (const DRAMCommand& c)
                                            {
                                                return b.open_row_ == dram_row(c.trans.address);
                                            });
            if (any_pending_hits && b.num_cas_to_open_row_ < 4)
                return out;
            // Otherwise we are good:
            out = DRAMCommand(addr, DRAMCommandType::PRECHARGE);
        }
    } else {
        out = DRAMCommand(addr, DRAMCommandType::ACTIVATE);
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

sel_cmd_t
FRRFCFS(cmdq_iterator cmd_it, DRAMBank& b)
{
    sel_cmd_t out;
    if (cmd_is_write(cmd_it->type) && b.open_row_.has_value()) {
        bool any_pending_read_hits = std::any_of(std::next(cmd_it), b.cmd_queue_.end(),
                                        [b] (const DRAMCommand& c)
                                        {
                                            return cmd_is_read(c.type) 
                                                    && b.open_row_ == dram_row(c.trans.address);
                                        });
        if (any_pending_read_hits)
            return out;
    }
    return FRFCFS(cmd_it, b);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

sel_cmd_t
ARRFCFS(cmdq_iterator cmd_it, DRAMBank& b)
{
    sel_cmd_t out;
    // Only issue writes if we have switched modes.`
    if (cmd_is_write(cmd_it->type) && b.write_draining_ == 0)
        return out;

    uint64_t addr = cmd_it->trans.address;
    size_t r = dram_row(addr);
    if (b.open_row_.has_value()) {
        if (b.open_row_ == r) {
            out = *cmd_it;
        } else {
            // Precharge:
            if (cmd_it != b.cmd_queue_.begin())
                return out;
            bool any_pending_hits = std::any_of(std::next(cmd_it), b.cmd_queue_.end(),
                                            [t=cmd_is_read(cmd_it->type), b] (const DRAMCommand& c)
                                            {
                                                return (cmd_is_read(c.type) == t)
                                                        && b.open_row_ == dram_row(c.trans.address);
                                            });
            if (any_pending_hits && b.num_cas_to_open_row_ < 4)
                return out;
            out = DRAMCommand(addr, DRAMCommandType::PRECHARGE);
        }
    } else {
        out = DRAMCommand(addr, DRAMCommandType::ACTIVATE);
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

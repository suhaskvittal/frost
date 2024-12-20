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

using subqueue_t = DRAMBank::queue_t;

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
FRFCFS(const subqueue_t& q, cmdq_iterator cmd_it, DRAMBank& b)
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
            if (cmd_it != q.begin())
                return out;
            bool any_pending_hits = std::any_of(std::next(cmd_it), q.end(),
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
FRRFCFS(const subqueue_t& q, cmdq_iterator cmd_it, DRAMBank& b)
{
    sel_cmd_t out;
    if (cmd_is_write(cmd_it->type) && any_read_hits_in_queue)
        return out;
    return FRFCFS(q, cmd_it, b);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

sel_cmd_t
ARRFCFS(const subqueue_t& q, cmdq_iterator cmd_it, DRAMBank& b, bool any_reads_in_queue, bool is_first_read)
{
    sel_cmd_t out;
    if (cmd_is_write(cmd_it->type) && any_reads_in_queue)
        return out;

    uint64_t addr = cmd_it->trans.address;
    size_t r = dram_row(addr);
    if (b.open_row_.has_value()) {
        if (b.open_row_ == r) {
            out = *cmd_it;
        } else {
            // Precharge:
            if (!is_first_read)
                return out;
            bool any_pending_hits = std::any_of(std::next(cmd_it), q.end(),
                                            [b] (const DRAMCommand& c)
                                            {
                                                return b.open_row_ == dram_row(c.trans.address);
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

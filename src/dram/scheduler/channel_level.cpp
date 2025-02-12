/*
 *  author: Suhas Vittal
 *  date:   11 February 2025
 * */

#include "dram/address.h"
#include "dram/scheduler/channel_level.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

ChannelLevelScheduler::ChannelLevelScheduler(DRAMChannel* c, const DRAMChannelState& s)
    :DRAMBaseScheduler(c, s)
{
    read_queue_.reserve(DRAM_RQ_SIZE);
    write_queue_.reserve(DRAM_WQ_SIZE);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

ChannelLevelScheduler::cmd_output_type
ChannelLevelScheduler::select_ready_command()
{
    auto& q = in_write_mode_ ? write_queue_ : read_queue_;

    SchedulerState s;
    bank_cmd_array bank_cmds(DRAM_TOT_BANKS_PER_CHANNEL);

    bool any_writes_were_possible = false;
    for (auto q_it = q.begin(); q_it != q.end(); q_it++)
    {
        DRAMCommand ready_cmd;
        size_t bank_idx = dram_bank_idx(q_it->trans.address);

        // Check if the bank already has a ready command:
        if (!cmd_is_invalid(std::get<0>(bank_cmds[bank_idx]).type))
            continue;

        size_t row = dram_row(q_it->trans.address);
        const auto& b = channel_get_const_bank_ref_from_idx(channel_state_, bank_idx);

        // If this is a write, check if it violates R->W dependency.
        if (in_write_mode_)
        {
            if (pending_reads_.count(q_it->trans.address))
                continue;
            any_writes_were_possible = true;
        }

        // Determine command to issue:
        DRAMCommandType type = DRAMCommandType::INVALID;
        if (b.open_row.has_value())
        {
            if (b.open_row == row)
                type = select_cas_command(q_it, q.end(), s, b);
            else if (!active_buffer_.count(bank_idx) && allow_demand_precharge(q_it, q.end(), s, b))
                type = DRAMCommandType::PRECHARGE;
        }
        else
        {
            type = DRAMCommandType::ACTIVATE;
        }
        ready_cmd = DRAMCommand(q_it->trans.address, type);

        // In transition condition: do not allow ACTs
        bool cmd_ok = !cmd_is_invalid(type)
                        && (!in_transition_ || !cmd_is_act(type))
                        && cmd_is_issuable(channel_state_, ready_cmd);
        if (cmd_ok)
            bank_cmds[bank_idx] = {ready_cmd, &q, q_it};
        
        // Update scheduler state:
        s.update_priority(bank_idx, q_it->trans.dram_issue_priority);
    }

    // If we could not do any writes despite being above the watermark, force a transition back to reads:
    if (in_write_mode_ && !any_writes_were_possible)
        in_transition_ = true;

    return select_from_bank_commands(bank_cmds);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline dram_rw_queue_type::const_iterator
search_for_inst_in_queue(const inst_ptr inst, const dram_rw_queue_type& q)
{
    return std::find_if(q.begin(), q.end(),
                    [inst] (const auto& e) { return e.trans.contains_inst(inst); });
}

bool
ChannelLevelScheduler::deadlock_find_inst(const inst_ptr inst) const
{
    DRAMBaseScheduler::deadlock_find_inst(inst);
    
    // Search for instruction in read queue:
    auto rd_it = search_for_inst_in_queue(inst, read_queue_);
    if (rd_it != read_queue_.end())
    {
        size_t p = std::distance(read_queue_.begin(), rd_it);
        std::cerr << "\tfound in read queue, position " << p << "\n";
        return true;
    }
    else
    {
        std::cerr << "\tnot in read queue\n";
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

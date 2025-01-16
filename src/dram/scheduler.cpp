/*
 *  author: Suhas Vittal
 *  date:   24 December 2024
 * */

#include "dram/cmd_args.h"
#include "dram/enums.h"
#include "dram/scheduler.h"

#include <algorithm>
#include <limits>
#include <iomanip>
#include <iostream>
#include <numeric>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

CommandScheduler::CommandScheduler(const DRAMChannelState& s)
    :state_(s)
{}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
CommandScheduler::can_accept(uint64_t address, bool is_write) const
{
    size_t ii = get_bank_idx(address);
    return cmd_queues_.at(ii).size() < DRAM_CMDQ_SIZE;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
CommandScheduler::has_no_pending_reads() const
{
    return std::all_of(cmd_queues_.begin(), cmd_queues_.end(),
                [] (const auto& q) 
                {
                    return q.num_reads() == 0;
                });
}

bool
CommandScheduler::has_no_pending_writes() const
{
    return std::all_of(cmd_queues_.begin(), cmd_queues_.end(),
                [] (const auto& q) 
                {
                    return q.num_writes() == 0;
                });
}

size_t
CommandScheduler::count_pending_reads() const
{
    return std::transform_reduce(cmd_queues_.begin(), cmd_queues_.end(), static_cast<size_t>(0),
                            std::plus<size_t>{},
                            [] (const auto& q)
                            {
                                return q.num_reads();
                            });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
CommandScheduler::enqueue(Transaction&& trans, DRAMCommandType t)
{
    size_t ii = get_bank_idx(trans.address);
    cmd_queues_[ii].emplace_back(std::move(trans), t);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

typename CommandScheduler::cmd_output_type
CommandScheduler::select_command()
{
    cmd_output_type out;
    global_write_mode_ = global_write_mode_ && !has_no_pending_writes();

    for (size_t i = 0; i < cmd_queues_.size(); i++)
    {
        auto& q = cmd_queues_[next_cmd_queue_idx_];
        const auto& b = get_bank_ref(next_cmd_queue_idx_);

        if (global_write_mode_)
        {
            if (q.num_writes() > 0)
                out = select_command_from_queue(q, b);
        }
        else
            out = select_command_from_queue(q, b);

        fast_increment_and_mod_inplace<DRAM_TOT_BANKS_PER_CHANNEL>(next_cmd_queue_idx_);
        if (!cmd_is_invalid(std::get<0>(out).type))
            break;
    }
    
    const auto& [ready_cmd, e] = out;
    if (cmd_is_write(ready_cmd.type))
    {
        ++write_burst_count_;
        if constexpr (DRAM_WRITE_POLICY == DRAMWritePolicy::SYNC)
            global_write_mode_ = true;
    }
    else if (cmd_is_read(ready_cmd.type) && write_burst_count_ > 0)
    {
        // Update write burst stats
        ++s_write_bursts_;
        s_max_writes_in_burst_ = std::max(s_max_writes_in_burst_, write_burst_count_);
        s_min_writes_in_burst_ = std::min(s_min_writes_in_burst_, write_burst_count_);
        write_burst_count_ = 0;
    }

    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
CommandScheduler::print_queue_state(std::ostream& out) const
{
    out << "CMDQ_START -------\n\n";
    for (size_t i = 0; i < DRAM_TOT_BANKS_PER_CHANNEL; i++)
    {
        if (i == next_cmd_queue_idx_)
            out << "--> ";
        else
            out << "    ";
        out << "BA" << std::setw(2) << std::left << i << "  : ";

        const auto& q = cmd_queues_.at(i);

        std::string q_contents;
        for (const auto& cmd : q)
        {
            if (cmd_is_read(cmd.type))
                q_contents += " RD";
            else
                q_contents += " WR";
        }

        out << std::setw(3*DRAM_CMDQ_SIZE+8) << std::left << q_contents
            << "size: " << std::setw(2) << std::right << q.size()
            << " of " << DRAM_CMDQ_SIZE << "\n";
    }
    out << "\nCMDQ_END -------\n";
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

typename CommandScheduler::cmd_output_type
CommandScheduler::select_command_from_queue(CmdQueue& q, const DRAMBankState& b)
{
    AlgoState s(b);

    DRAMCommand ready_cmd;
    std::optional<CmdQueueEntry> qe;
    for (auto cmd_it = q.begin(); cmd_it != q.end(); cmd_it++)
    {
        if (skip_command(cmd_it, q, s))
            continue;

        uint64_t addr = cmd_it->trans.address;
        DRAMCommandType type = DRAMCommandType::INVALID;
        if (b.open_row.has_value())
        {
            if (b.open_row == dram_row(addr))
                type = cmd_it->type;
            else if (allow_demand_precharge(cmd_it, q, s))
                type = DRAMCommandType::PRECHARGE;
        }
        else
            type = DRAMCommandType::ACTIVATE;
        ready_cmd = DRAMCommand(addr, type);

        if (!cmd_is_invalid(type) && cmd_is_issuable(state_, ready_cmd))
        {
            if (cmd_is_cas(type))
            {
                if (cmd_is_write(type))
                    --q.writes;
                cmd_it->is_row_buffer_hit = b.next_cas_is_row_buffer_hit;
                qe.emplace(std::move(*cmd_it));
                q.erase(cmd_it);
            }
            break;
        }
        else
            ready_cmd.type = DRAMCommandType::INVALID;

        s.is_first = false;
    }
    return std::make_tuple(ready_cmd, qe);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
CommandScheduler::skip_command(CmdQueue::const_iterator cmd_it, const CmdQueue& q, const AlgoState& s)
{
    if (global_write_mode_ && cmd_is_read(cmd_it->type))
        return true;
    return false;
}

bool
CommandScheduler::allow_demand_precharge(CmdQueue::const_iterator cmd_it, const CmdQueue& q, const AlgoState& s)
{
    if constexpr (DRAM_SCHED_POLICY == DRAMSchedPolicy::FCFS)
        return true;
    else if constexpr (DRAM_SCHED_POLICY == DRAMSchedPolicy::FRFCFS)
    {
        bool any_pending_hits = std::any_of(std::next(cmd_it), q.end(),
                                    [write_mode=global_write_mode_,
                                    row=s.bank.open_row.value()]
                                    (const auto& e)
                                    {
                                        if (write_mode && cmd_is_read(e.type))
                                            return false;
                                        return row == dram_row(e.trans.address);
                                    });
        return s.is_first && (!any_pending_hits || s.bank.num_cas_to_open_row >= 4);
    }
    else
        return false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

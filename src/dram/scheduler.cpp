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

    update_write_mode();
    for (size_t i = 0; i < cmd_queues_.size(); i++)
    {
        auto& q = cmd_queues_[next_cmd_queue_idx_];
        const auto& b = get_bank_ref(next_cmd_queue_idx_);

        size_t& write_cnt = write_counters_[next_cmd_queue_idx_];
        if (global_write_mode_)
        {
            if (write_cnt > 0)
            {
                out = select_command_from_queue(q, b);
                const auto& [ready_cmd, e] = out;
                if (cmd_is_write(ready_cmd.type))
                    --write_cnt;
            }
        }
        else
            out = select_command_from_queue(q, b);

        fast_increment_and_mod_inplace<TOT_BANKS>(next_cmd_queue_idx_);
        if (!cmd_is_invalid(std::get<0>(out).type))
            break;
    }
    
    const auto& [ready_cmd, e] = out;
    if (cmd_is_write(ready_cmd.type))
        ++write_burst_count_;
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
    for (size_t i = 0; i < TOT_BANKS; i++)
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
    if constexpr (DRAM_WRITE_POLICY == DRAMWritePolicy::SYNC)
    {
        if (global_write_mode_ == cmd_is_read(cmd_it->type))
            return true;
    }
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
                                    [row=s.bank.open_row.value(), cmd_type=cmd_it->type]
                                    (const auto& e)
                                    {
                                        if constexpr (DRAM_WRITE_POLICY == DRAMWritePolicy::SYNC)
                                        {
                                            if (cmd_type != e.type)
                                                return false;
                                        }
                                        return row == dram_row(e.trans.address);
                                    });
        return s.is_first && (!any_pending_hits || s.bank.num_cas_to_open_row >= 4);
    }
    else
        return false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
CommandScheduler::update_write_mode()
{
    if constexpr (DRAM_WRITE_POLICY == DRAMWritePolicy::SYNC)
    {
        constexpr size_t SWITCH_THRESHOLD = DRAM_CMDQ_SIZE / 4;

        if (!global_write_mode_)
        {
            auto q_it = std::find_if(cmd_queues_.begin(), cmd_queues_.end(),
                                    [no_pending=has_no_pending_reads()] (const auto& q)
                                    { 
                                        return (no_pending || q.size() >= DRAM_CMDQ_SIZE)
                                                && q.num_writes() > SWITCH_THRESHOLD;
                                    });
            if (q_it != cmd_queues_.end())
                enter_write_mode();
        }
        else
        {
            global_write_mode_ = std::any_of(write_counters_.begin(), write_counters_.end(),
                                        [] (size_t x) { return x != 0; });
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
CommandScheduler::enter_write_mode()
{
    std::array<size_t, TOT_BANKS> write_cnts;
    std::transform(cmd_queues_.begin(), cmd_queues_.end(), write_cnts.begin(),
            [] (const auto& q) { return q.num_writes(); });

    size_t writes = std::reduce(write_cnts.begin(), write_cnts.end()) / TOT_BANKS;
    writes = std::max(writes, static_cast<size_t>(1));
    
    write_counters_.fill(OPT_DRAM_WRITE_SYNC_COUNT);
//  write_counters_.fill(writes);
    for (size_t i = 0; i < TOT_BANKS; i++)
        write_counters_[i] = std::min(cmd_queues_[i].num_writes(), write_counters_[i]);
    global_write_mode_ = true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

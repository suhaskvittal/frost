/*
 *  author: Suhas Vittal
 *  date:   24 December 2024
 * */

#include "dram/scheduler.h"
#include "util/numerics.h"

#include <limits>
#include <numeric>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

CommandScheduler::CommandScheduler(const DRAMChannelState& s)
    :state_(s)
{
    for (size_t i = 0; i < TOT_BANKS; i++)
    {
        size_t ba = fast_mod<DRAM_BANKS>(i),
               bg = fast_mod<DRAM_BANKGROUPS>(i >> numeric_traits<DRAM_BANKS>::log2),
               ra = fast_mod<DRAM_RANKS>(i >> numeric_traits<DRAM_BANKGROUPS*DRAM_BANKS>::log2);
        cmd_queues_[i].bank_p_ = &s.at(ra).at(bg).at(ba);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
CommandScheduler::can_accept(uint64_t address, bool is_write)
{
    size_t ii = get_bank_idx(address);
    bool out = cmd_queues_[ii].can_accept(is_write);
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
CommandScheduler::has_no_pending_reads() const
{
    return std::all_of(cmd_queues_.begin(), cmd_queues_.end(),
                [] (const auto& q) { return q.has_no_pending_reads(); });
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
CommandScheduler::enqueue(Transaction&& trans, DRAMCommandType t)
{
    size_t ii = get_bank_idx(trans.address);
    cmd_queues_[ii].enqueue(std::move(trans), t);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

typename CommandScheduler::cmd_queue_t::cmd_output_t
CommandScheduler::select_command()
{
    if constexpr (DRAM_WRITE_POLICY == DRAMWritePolicy::ALAP_SYNC)
        alap_sync_update_write_mode();

    cmd_queue_t::cmd_output_t out;

    for (size_t i = 0; i < cmd_queues_.size(); i++)
    {
        auto& q = cmd_queues_[next_cmd_queue_idx_];

        size_t& write_cnt = alap_sync_write_tracker_[next_cmd_queue_idx_];
        if (alap_sync_in_write_mode_)
        {
            if (q.num_writes() == 0)
                write_cnt = 0;
            else if (write_cnt > 0)
            {
                out = q.select_command(state_, true);
                if (cmd_is_write(std::get<0>(out).type))
                    --write_cnt;
            }
        }
        else
            out = q.select_command(state_);

        fast_increment_and_mod_inplace<TOT_BANKS>(next_cmd_queue_idx_);
        if (!cmd_is_invalid(std::get<0>(out).type))
            break;
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
CommandScheduler::print_queue_state(std::ostream& out)
{
    out << "CMDQ_START -------\n\n";
    for (size_t i = 0; i < TOT_BANKS; i++)
    {
        if (i == next_cmd_queue_idx_)
            out << "--> ";
        else
            out << "    ";
        out << "BA" << std::setw(2) << std::left << i << "  : ";
        cmd_queues_.at(i).print_queue_contents(out);
        out << "\n";
    }
    out << "\nCMDQ_END -------\n";
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
CommandScheduler::alap_sync_update_write_mode()
{
    if (alap_sync_in_write_mode_)
    {
        bool all_done = std::all_of(alap_sync_write_tracker_.begin(), alap_sync_write_tracker_.end(),
                                    [] (size_t x) { return x == 0; });
        alap_sync_in_write_mode_ = !all_done;
    }
    else
    {
        bool drain_cond_1 = has_no_pending_reads()
                        && std::any_of(cmd_queues_.begin(), cmd_queues_.end(),
                                [] (const auto& q)
                                {
                                    return q.num_writes() > cmd_queue_t::ALAP_MAX_WRITES;
                                });
        bool drain_cond_2 = std::any_of(cmd_queues_.begin(), cmd_queues_.end(),
                                [] (const auto& q)
                                {
                                    return !q.can_accept(true) && q.num_writes() > cmd_queue_t::ALAP_MAX_WRITES;
                                });
        if (drain_cond_1 || drain_cond_2)
            alap_sync_enter_write_mode();
    }
}

void
CommandScheduler::alap_sync_enter_write_mode()
{
    size_t max_writes = std::transform_reduce(cmd_queues_.begin(), cmd_queues_.end(),
                                    0,
                                    [] (size_t x, size_t y) { return std::max(x,y); },
                                    [] (const auto& q)
                                    {
                                        return q.num_writes();
                                    });
    size_t min_writes = std::transform_reduce(cmd_queues_.begin(), cmd_queues_.end(),
                                    std::numeric_limits<size_t>::max(),
                                    [] (size_t x, size_t y) { return std::min(x,y); },
                                    [] (const auto& q)
                                    {
                                        return q.num_writes();
                                    });
    size_t writes;
    if (DRAM_PAGE_POLICY == DRAMPagePolicy::OPEN)
        writes = max_writes;
    else
        writes = std::max(min_writes, static_cast<size_t>(1));
    // Setup state for write moder:
    alap_sync_in_write_mode_ = true;
    alap_sync_write_tracker_.fill(writes);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

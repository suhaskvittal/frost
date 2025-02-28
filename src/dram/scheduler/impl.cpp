/*
 *  author: Suhas Vittal
 *  date:   12 February 2025
 * */

#include "dram/scheduler.h"

/*
 * This file only contains the implementation of the per-bank scheduler and
 * channel-wide arbiter.
 * */

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMScheduler::cmd_output_type
DRAMScheduler::select_from_bank_commands(bank_cmd_array&& bank_cmds)
{
    // Implementation of arbiter is round-robin -- uses `next_bank_idx_` to determine
    // starting bank:
    DRAMCommand ready_cmd;
    std::optional<RWQueueEntry> q_entry;

    auto next_bank_it = bank_cmds.begin() + next_bank_idx_;
    
    // Find valid command -- start from the next bank that should be prioritized
    bool found = true;

    auto cmd_it = std::find_if(next_bank_it, bank_cmds.end(),
                        [] (const auto& x) { return x.has_value(); });

    // If we failed, try again but start from the beginning:
    if (cmd_it == bank_cmds.end())
    {
        cmd_it = std::find_if(bank_cmds.begin(), next_bank_it,
                        [] (const auto& x) { return x.has_value(); });
        found = (cmd_it != next_bank_it);
    }

    // Read iterator:
    if (found)
    {
        dram_rw_queue_type* q_p;
        dram_rw_queue_type::iterator q_it;
        std::tie(ready_cmd, q_p, q_it) = cmd_it->value();

        size_t bank_idx = dram_bank_idx(ready_cmd.address);
        if (ready_cmd.is_cas())
        {
            const auto& b = channel_get_const_bank_ref_from_idx(channel_state_, bank_idx);
            q_it->is_row_buffer_hit = b.next_cas_is_row_buffer_hit;
            q_entry.emplace(std::move(*q_it));
            q_p->erase(q_it);

            // Update `pending` structures:
            auto& p = ready_cmd.is_read() ? pending_reads_ : pending_writes_;

            // Erase one entry of `address` from `p` -- now, we have to check if there any more:
            p.erase(p.find(ready_cmd.address));

            // If there are any more entries in `p` that match `address`, delete the corresponding entries
            // in `*q_p`
            if (p.find(ready_cmd.address) != p.end())
            {
                // Search and eliminate any additional commands in `p` that match this address:
                auto it = std::remove_if(q_p->begin(), q_p->end(),
                                        [addr=ready_cmd.address] 
                                        (const auto& e) { return e.trans.address == addr; });

                q_p->erase(it, q_p->end());
                p.erase(ready_cmd.address);
            }

            // Also, remove entry in active buffer:
            active_buffer_[bank_idx].reset();
        }
        else if (ready_cmd.is_act())
        {
            // Insert into bank idx to ensure this is used:
            active_buffer_[bank_idx] = {q_p, ready_cmd.address};
        }
        next_bank_idx_ = fast_mod(bank_idx+1, DRAM_TOT_BANKS_PER_CHANNEL);
    }

    return cmd_output_type{ready_cmd, q_entry};
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
DRAMScheduler::allow_demand_precharge(
        dram_rw_queue_type::const_iterator q_it,
        dram_rw_queue_type::const_iterator q_end,
        const SchedulerState& s,
        const DRAMBankState& b)
{
    constexpr size_t MAX_Q_SIZE = std::max(DRAM_RQ_SIZE, DRAM_WQ_SIZE);

    if constexpr (DRAM_SCHED_POLICY == DRAMSchedPolicy::FCFS)
        return true;

    size_t bank_idx = dram_bank_idx(q_it->trans.address);

    // Note that this is the same as checking if a cmd is first (as `highest_priority = -128` at the beginning
    // of the queue)
    if (s.highest_priority.at(bank_idx) >= q_it->trans.dram_issue_prio)
        return false;

    // Get commands relevant to this bank:
    std::vector<RWQueueEntry> cmds;
    cmds.reserve(MAX_Q_SIZE);

    std::copy_if(std::next(q_it), q_end, std::back_inserter(cmds),
            [bank_idx] (const auto& e)
            {
                return dram_bank_idx(e.trans.address) == bank_idx;
            });

    // Check if there are any instructions with higher priority than this:
    if constexpr (dram_sched_obey_issue_priority(DRAM_SCHED_POLICY))
    {
        // Now check in front of `q_it`
        bool any_higher_priority = std::any_of(cmds.begin(), cmds.end(),
                                        [p=q_it->trans.dram_issue_prio] 
                                        (const auto& e)
                                        {
                                            return e.trans.dram_issue_prio > p;
                                        });
        if (any_higher_priority)
            return false;
    }

    // Check for row buffer hits:
    if constexpr (dram_sched_prioritize_row_buffer_hits(DRAM_SCHED_POLICY))
    {
        bool not_enough_hits = OPT_DRAM_CLOSE_ROW_AFTER_NUM_HITS < 0 
                                || b.num_cas_to_open_row < OPT_DRAM_CLOSE_ROW_AFTER_NUM_HITS;

        bool any_pending_row_hits = std::any_of(cmds.begin(), cmds.end(),
                                        [row=b.open_row.value()] 
                                        (const auto& e) { return dram_row(e.trans.address) == row; });

        if (any_pending_row_hits && not_enough_hits)
            return false;
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
DRAMScheduler::enable_autopre(
        dram_rw_queue_type::const_iterator q_it,
        dram_rw_queue_type::const_iterator q_end,
        const SchedulerState& st,
        const DRAMBankState& b)
{
    if constexpr (DRAM_PAGE_POLICY == DRAMPagePolicy::OPEN)
    {
        return false;
    }
    else if constexpr (DRAM_PAGE_POLICY == DRAMPagePolicy::CLOSE)
    {
        return true;
    }
    else
    {
        size_t bank_idx = dram_bank_idx(q_it->trans.address);

        bool do_autopre = false;

        do_autopre |= q_it->trans.is_read() == in_write_mode_;

        // Predict if we will start transitioning:
        constexpr int X_TOL = 4;

        do_autopre |= (in_write_mode_ && read_occu() != 0 && write_occu() <= low_watermark_+X_TOL);
        do_autopre |= (!in_write_mode_ && (read_occu() < X_TOL || write_occu() >= high_watermark_-X_TOL));

        if (do_autopre)
            return true;

        // For ease of checking, get all commands in the queue that belong to the given bank:
        std::vector<RWQueueEntry> cmds;
        cmds.reserve(128);
        std::copy_if(std::next(q_it), q_end, std::back_inserter(cmds),
                        [bank_idx] (const auto& e) { return dram_bank_idx(e.trans.address) == bank_idx; });

        // Check for row buffer hits:
        if constexpr (dram_sched_prioritize_row_buffer_hits(DRAM_SCHED_POLICY))
        {
            bool any_pending_hits = std::any_of(cmds.begin(), cmds.end(),
                                            [row=b.open_row.value()]
                                            (const auto& e) { return dram_row(e.trans.address) == row; });

            do_autopre |= (!any_pending_hits || b.num_cas_to_open_row >= 3);
        }

        return do_autopre;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

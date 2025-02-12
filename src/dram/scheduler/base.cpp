/*
 *  author: Suhas Vittal
 *  date:   11 February 2025
 * */

#include "dram/scheduler/base.h"
#include "dram/channel.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMBaseScheduler::DRAMBaseScheduler(DRAMChannel* c, const DRAMChannelState& s)
    :owning_channel_(c),
    channel_state_(s),
    low_watermark_(DRAM_WQ_SIZE * OPT_DRAM_LOW_WATERMARK),
    high_watermark_(DRAM_WQ_SIZE * OPT_DRAM_HIGH_WATERMARK)
{
    pending_reads_.reserve(DRAM_RQ_SIZE);
    pending_writes_.reserve(DRAM_WQ_SIZE);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
DRAMBaseScheduler::add_incoming(Transaction trans)
{
    // Check for write forwarding:
    if (pending_writes_.count(trans.address))
    {
        if (trans_is_read(trans.type))
            owning_channel_->outgoing_queue_.emplace(std::move(trans), GL_DRAM_CYCLE+1);
        return true;
    }

#if defined(DRAM_RANDOMIZE_WRITE_ADDRESSES)
    // We want to randomize the write address here:
    if (trans_is_write(trans.type))
    {
        // Clear out the rank, bankgroup, and bank bits:
        trans.address &= ~((DRAM_TOT_BANKS_PER_CHANNEL-1) << BG_OFF);

        // Now set the to the given idx:
        trans.address |= dram_randomize_write_addresses_bank_idx_ << BG_OFF;

        // Move to next bank:
        fast_increment_and_mod_inplace<DRAM_TOT_BANKS_PER_CHANNEL>(dram_randomize_write_addresses_bank_idx_);
    }
#endif

    // Add to requisite queue:
    auto& p = trans_is_read(trans.type) ? pending_reads_ : pending_writes_;
    p.insert(trans.address);

    return add_to_rw_queue(trans);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMBaseScheduler::handle_preab_forced_transition()
{
    active_buffer_.clear();

    bool was_in_write_mode = in_write_mode_;

    in_transition_ = false;
    in_write_mode_ = false;

    if (was_in_write_mode)
        owning_channel_->update_modal_stats_post_transition();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
DRAMBaseScheduler::deadlock_find_inst(const inst_ptr inst) const
{
    std::cerr << "scheduler:\n"
                << "\tread occupancy = " << read_occu() << "\n"
                << "\twrite occupancy = " << write_occu() << "\n"
                << "\tin write mode = " << in_write_mode_ << "\n"
                << "\tin transition = " << in_transition_ << "\n"
                << "\tactive buffer contents (size = " << active_buffer_.size() << "):";
    for (size_t idx : active_buffer_)
        std::cerr << " " << idx;
    std::cerr << "\n";

    return false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMBaseScheduler::try_switch_to_reads()
{
    if (!in_write_mode_ || in_transition_)
        return;

    in_transition_ = (read_occu() > 0) && (write_occu() < low_watermark_);
}

void
DRAMBaseScheduler::try_switch_to_writes()
{
    if (in_write_mode_ || in_transition_)
        return;

    bool drain_cond_1 = (read_occu() == 0) && (write_occu() > 0),
         drain_cond_2 = (write_occu() >= high_watermark_) || any_write_queues_full();

    in_transition_ = drain_cond_1 || drain_cond_2;

#if defined(DRAM_TRACK_ADVANCED_STATS)
    // Compute write counts (using `pending_writes_`)
    if (in_transition_)
    {
        write_counts_array write_cnts{};

        for (uint64_t x : pending_writes_)
        {
            size_t bank_idx = dram_bank_idx(x);
            ++write_cnts[bank_idx];
        }

        dram_update_write_distribution_stats(
                    write_cnts, 
                    owning_channel_->s_tot_write_queue_std_,
                    owning_channel_->s_tot_write_queue_minmax_diff_);
    }
#endif
}

void
DRAMBaseScheduler::try_to_transition()
{
    if (in_transition_ && active_buffer_.empty())
    {
        in_transition_ = false;
        in_write_mode_ = !in_write_mode_;

        owning_channel_->update_modal_stats_post_transition();
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline void
pending_erase_one(std::unordered_multiset<uint64_t>& p, uint64_t address)
{
    p.erase(p.find(address));
}

DRAMBaseScheduler::cmd_output_type
DRAMBaseScheduler::select_from_bank_commands(bank_cmd_array bank_cmds)
{
    // Implementation of arbiter is round-robin -- uses `next_bank_idx_` to determine
    // starting bank:
    DRAMCommand ready_cmd;
    std::optional<RWQueueEntry> q_entry;
    
    // Rotate `bank_cmds` so `next_bank_idx_` comes first:
    std::rotate(bank_cmds.begin(), bank_cmds.begin() + next_bank_idx_, bank_cmds.end());
    
    // Find valid command:
    auto cmd_it = std::find_if(bank_cmds.begin(), bank_cmds.end(),
                        [] (const auto& x) { return !cmd_is_invalid(std::get<0>(x).type); });

    // Read iterator:
    if (cmd_it != bank_cmds.end())
    {
        dram_rw_queue_type* q_p;
        dram_rw_queue_type::iterator q_it;
        std::tie(ready_cmd, q_p, q_it) = *cmd_it;

        size_t bank_idx = dram_bank_idx(ready_cmd.address);
        if (cmd_is_cas(ready_cmd.type))
        {
            const auto& b = channel_get_const_bank_ref_from_idx(channel_state_, bank_idx);
            q_it->is_row_buffer_hit = b.next_cas_is_row_buffer_hit;
            q_entry.emplace(std::move(*q_it));
            q_p->erase(q_it);

            // Also, remove entry in active buffer:
            active_buffer_.erase(bank_idx);

            // Update `pending` structures:
            auto& p = cmd_is_read(ready_cmd.type) ? pending_reads_ : pending_writes_;
            if (p.count(ready_cmd.address) > 1)
            {
                // Search and eliminate any additional commands in `p` that match this address:
                for (auto it = q_p->begin(); it != q_p->end(); )
                {
                    if (it->trans.address == ready_cmd.address)
                        it = q_p->erase(it);
                    else
                        ++it;
                }
            }
            p.erase(ready_cmd.address);
        }
        else if (cmd_is_act(ready_cmd.type))
        {
            // Insert into bank idx to ensure this is used:
            active_buffer_.insert(bank_idx);
        }
        next_bank_idx_ = fast_mod<DRAM_TOT_BANKS_PER_CHANNEL>(bank_idx+1);
    }

    return cmd_output_type{ready_cmd, q_entry};
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
DRAMBaseScheduler::allow_demand_precharge(
        dram_rw_queue_type::const_iterator q_it,
        dram_rw_queue_type::const_iterator q_end,
        const SchedulerState& s,
        const DRAMBankState& b)
{
    if constexpr (DRAM_SCHED_POLICY == DRAMSchedPolicy::FCFS)
        return true;

    size_t bank_idx = dram_bank_idx(q_it->trans.address);

    // Note that this is the same as checking if a cmd is first (as `highest_priority = -128` at the beginning
    // of the queue)
    if (s.highest_priority.at(bank_idx) >= q_it->trans.dram_issue_priority)
        return false;

    // Get commands relevant to this bank:
    std::vector<RWQueueEntry> cmds;
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
                                        [p=q_it->trans.dram_issue_priority] 
                                        (const auto& e)
                                        {
                                            return e.trans.dram_issue_priority > p;
                                        });
        if (any_higher_priority)
            return false;
    }

    // Check for row buffer hits:
    if constexpr (dram_sched_prioritize_row_buffer_hits(DRAM_SCHED_POLICY))
    {
        bool any_pending_row_hits = std::any_of(cmds.begin(), cmds.end(),
                                        [row=b.open_row.value()] 
                                        (const auto& e) { return dram_row(e.trans.address) == row; });
        if (any_pending_row_hits && b.num_cas_to_open_row < 4)
            return false;
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMCommandType
DRAMBaseScheduler::select_cas_command(
        dram_rw_queue_type::const_iterator q_it,
        dram_rw_queue_type::const_iterator q_end,
        const SchedulerState& st,
        const DRAMBankState& b)
{
    DRAMClosureHint hint = q_it->trans.dram_closure_hint;

    DRAMCommandType cmd =         in_write_mode_ ? DRAMCommandType::WRITE 
                                                 : DRAMCommandType::READ,
                    cmd_autopre = in_write_mode_ ? DRAMCommandType::WRITE_PRECHARGE 
                                                 : DRAMCommandType::READ_PRECHARGE;

    if constexpr (DRAM_PAGE_POLICY == DRAMPagePolicy::OPEN)
    {
        return cmd;
    }
    else if constexpr (DRAM_PAGE_POLICY == DRAMPagePolicy::CLOSE)
    {
        return cmd_autopre;
    }
    else
    {
        size_t bank_idx = dram_bank_idx(q_it->trans.address);

        // If we are in transition, then we know that there will be no more activations, nor
        // row buffer hits.
        if (in_transition_)
            return cmd_autopre;

        // Predict if we will start transitioning:
        if (in_write_mode_ && read_occu() != 0 && write_occu() <= low_watermark_+4)
            return cmd_autopre;
        if (!in_write_mode_ && (read_occu() < 4 || write_occu() >= high_watermark_-4))
            return cmd_autopre;

        // For ease of checking, get all commands in the queue that belong to the given bank:
        std::vector<RWQueueEntry> cmds;
        std::copy_if(std::next(q_it), q_end, std::back_inserter(cmds),
                        [bank_idx] (const auto& e) { return dram_bank_idx(e.trans.address) == bank_idx; });

        // Check for row buffer hits:
        if constexpr (dram_sched_prioritize_row_buffer_hits(DRAM_SCHED_POLICY))
        {
            bool any_pending_hits = std::any_of(cmds.begin(), cmds.end(),
                                            [row=b.open_row.value()]
                                            (const auto& e) { return dram_row(e.trans.address) == row; });
            if (!any_pending_hits || b.num_cas_to_open_row >= 3)
                return cmd_autopre;
        }

        return cmd;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

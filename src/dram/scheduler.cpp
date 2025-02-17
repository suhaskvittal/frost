/*
 *  author: Suhas Vittal
 *  date:   12 February 2025
 * */

#include "dram/channel.h"
#include "dram/scheduler.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMScheduler::DRAMScheduler(DRAMChannel* c, const DRAMChannelState& s)
    :owning_channel_(c),
    channel_state_(s),
    low_watermark_(DRAM_WQ_SIZE * DRAM_QUEUE_COUNT * OPT_DRAM_LOW_WATERMARK),
    high_watermark_(DRAM_WQ_SIZE * DRAM_QUEUE_COUNT * OPT_DRAM_HIGH_WATERMARK)
{
    pending_reads_.reserve(DRAM_QUEUE_COUNT*DRAM_RQ_SIZE);
    pending_writes_.reserve(DRAM_QUEUE_COUNT*DRAM_WQ_SIZE);

    for (auto& q : read_queues_)
        q.reserve(DRAM_RQ_SIZE);

    for (auto& q : write_queues_)
        q.reserve(DRAM_WQ_SIZE);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
DRAMScheduler::add_incoming(Transaction trans)
{
    if (pending_writes_.find(trans.address) != pending_writes_.end())
    {
        if (trans_is_read(trans.type))
            owning_channel_->outgoing_queue_.emplace(trans, GL_DRAM_CYCLE+1);
        return true;
    }

#if defined(DRAM_RANDOMIZE_WRITE_ADDRESSES)
    if (trans_is_write(trans.type))
    {
        // Clear out the rank, bankgroup, and bank bits (assumed is contiguous region of address);
        trans.address &= ~((DRAM_TOT_BANKS_PER_CHANNEL-1) << BG_OFF);

        // Set bank idx
        trans.address |= dram_randomize_write_addresses_bank_idx_ << BG_OFF;

        fast_increment_and_mod_inplace<DRAM_TOT_BANKS_PER_CHANNEL>(dram_randomize_write_addresses_bank_idx_);
    }
#endif

    size_t q_idx = dram_s_queue_index(trans.address);
    auto& q =       trans_is_read(trans.type) ? read_queues_[q_idx] : write_queues_[q_idx];
    auto& p =       trans_is_read(trans.type) ? pending_reads_      : pending_writes_;
    size_t s =      trans_is_read(trans.type) ? DRAM_RQ_SIZE        : DRAM_WQ_SIZE;

    if (q.size() < s)
    {
        p.insert(trans.address);
        q.push_back(trans);
        return true;
    }
    
    return false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMScheduler::cmd_output_type
DRAMScheduler::select_ready_command()
{
    constexpr size_t BANKS_PER_QUEUE = DRAM_TOT_BANKS_PER_CHANNEL/DRAM_QUEUE_COUNT;

    auto& q_array = in_write_mode_ ? write_queues_ : read_queues_;

    bank_cmd_array bank_cmds{};
    SchedulerState s{};
    
    // Determine initial queue index from `next_bank_idx_`
    size_t q_idx = next_bank_idx_ >> numeric_traits<BANKS_PER_QUEUE>::log2;
    bool any_writes_are_possible = false;
    for (size_t i = 0; i < DRAM_QUEUE_COUNT; i++)
    {
        auto& q = q_array[q_idx];
        
        // Accumulate bank commands from this queue:
        size_t bank_cmds_found = 0;
        for (auto q_it = q.begin(); q_it != q.end(); q_it++)
        {
            DRAMCommand ready_cmd;
            size_t bank_idx = dram_bank_idx(q_it->trans.address);

            // Check if the bank already has a ready command:
            if (bank_cmds[bank_idx].has_value())
                continue;
            
            size_t row = dram_row(q_it->trans.address);
            const auto& b = channel_get_const_bank_ref_from_idx(channel_state_, bank_idx);

            // If this is a write, check if it violates R->W dependency.
            if (in_write_mode_)
            {
                if (pending_reads_.find(q_it->trans.address) != pending_reads_.end())
                    continue;

                any_writes_are_possible = true;
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
            {
                bank_cmds[bank_idx].emplace(ready_cmd, &q, q_it);
                ++bank_cmds_found;
            }
            
            // Update scheduler state:
            s.update_priority(bank_idx, q_it->trans.dram_issue_priority);
        }

        // We can exit early if we have found any bank commands, as we can now find a ready command
        if (bank_cmds_found > 0)
            break;
        
        // Otherwise, goto the next queue:
        fast_increment_and_mod_inplace<DRAM_QUEUE_COUNT>(q_idx);
    }

    if (in_write_mode_ && !any_writes_are_possible)
        in_transition_ = true;

    return select_from_bank_commands(std::move(bank_cmds));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMScheduler::handle_preab_forced_transition()
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
DRAMScheduler::deadlock_find_inst(const inst_ptr inst) const
{
    std::cerr << "scheduler:\n"
                << "\tread occupancy = " << read_occu() << "\n"
                << "\twrite occupancy = " << write_occu() 
                << " (low = " << low_watermark_ << ", high = " << high_watermark_ << ")\n"
                << "\tin write mode = " << in_write_mode_ << "\n"
                << "\tin transition = " << in_transition_ << "\n"
                << "\tactive buffer size = " << active_buffer_.size() << "\n";
    for (size_t i = 0; i < DRAM_QUEUE_COUNT; i++)
    {
        const auto& q = read_queues_.at(i);
        if (q.empty())
            continue;

        std::cerr << "in read queue " << i << " (size = " << q.size() << ") ...\t";

        auto rd_it = std::find_if(q.begin(), q.end(),
                                [inst] (const auto& x) { return x.trans.contains_inst(inst); });
        if (rd_it == q.end())
        {
            std::cerr << "not in queue\n";
        }
        else
        {
            size_t p = std::distance(q.begin(), rd_it);
            std::cerr << "found at position " << p << "\n";
            return true;
        }
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMScheduler::try_switch_to_reads()
{
    if (!in_write_mode_ || in_transition_)
        return;

    bool banks_have_met_write_quota = std::all_of(min_writes_per_bank_.begin(), min_writes_per_bank_.end(),
                                                [] (ssize_t w) { return w <= 0; });

    in_transition_ = read_occu() > 0
                        && (write_occu() <= low_watermark_) 
                        && !any_write_queues_full()
                        && banks_have_met_write_quota;
}

void
DRAMScheduler::try_switch_to_writes()
{
    if (in_write_mode_ || in_transition_)
        return;

    in_transition_ = (read_occu() == 0 && write_occu() > 0)
                     || (write_occu() >= high_watermark_ || any_write_queues_full());

    // Compute write counts (using `pending_writes_`)
    if (in_transition_)
    {
        write_counts_array write_cnts{};
        for (uint64_t x : pending_writes_)
        {
            size_t bank_idx = dram_bank_idx(x);
            ++write_cnts[bank_idx];
        }

        // If we are synchronizing writes, then we require that each bank perform at least
        // the minimum number of writes across all banks.
        if constexpr (DRAM_WRITE_POLICY == DRAMWritePolicy::SYNC)
        {
            size_t min = *std::min_element(write_cnts.begin(), write_cnts.end());
            min_writes_per_bank_.fill(min);
        }
        else
        {
            min_writes_per_bank_.fill(0);
        }

#if defined(DRAM_TRACK_ADVANCED_STATS)
        dram_update_write_distribution_stats(
                    write_cnts, 
                    owning_channel_->s_tot_write_queue_std_,
                    owning_channel_->s_tot_write_queue_minmax_diff_);
#endif

    }
}

void
DRAMScheduler::try_to_transition()
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

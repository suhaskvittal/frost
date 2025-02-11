/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "globals.h"
#include "dram_timing.h"
#include "memsys.h"

#include "dram/address.h"
#include "dram/channel.h"
#include "dram/cmd_args.h"
#include "dram/state.h"
#include "util/numerics.h"

#include <iomanip>

//#define DRAM_ENABLE_LOGGER

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

template <class T> inline T sqr(T x) { return x*x; }

void
dram_update_write_distribution_stats(const write_counts_array& cnts, double& std, uint32_t& diff)
{
    std += vec_std(cnts);

    auto [min_it, max_it] = std::minmax_element(cnts.begin(), cnts.end());
    diff += (*max_it) - (*min_it);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMChannel::DRAMChannel(size_t channel_id, double freq_ghz)
    :freq_ghz_(freq_ghz),
    channel_id_(channel_id),
    low_watermark_(OPT_DRAM_LOW_WATERMARK*DRAM_WQ_SIZE),
    high_watermark_(OPT_DRAM_HIGH_WATERMARK*DRAM_WQ_SIZE),
#if defined(DRAM_ENABLE_LOGGER)
    dram_logger_("dram_channel." + std::to_string(channel_id) + ".log")
#else
    dram_logger_()
#endif
{
    read_queue_.reserve(DRAM_RQ_SIZE);
    write_queue_.reserve(DRAM_WQ_SIZE);
    pending_reads_.reserve(DRAM_RQ_SIZE);
    pending_writes_.reserve(DRAM_WQ_SIZE);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::tick()
{
#if defined(DRAM_ENABLE_LOGGER)
    tmp_logger_.str("");

    bool any_ranks_in_refresh = std::any_of(state_.begin(), state_.end(),
                                        [] (const auto& ra)
                                        {
                                            return GL_DRAM_CYCLE >= ra.next_ref_cycle;
                                        });
    bool any_ranks_in_trfc = std::any_of(state_.begin(), state_.end(),
                                        [] (const auto& ra)
                                        {
                                            return GL_DRAM_CYCLE < ra.next_cmd_post_ref_cycle;
                                        });

    tmp_logger_ 
        << "========================= DRAM CYCLE " << GL_DRAM_CYCLE << " ===============================\n";

    tmp_logger_ << " }, in REF: " << (any_ranks_in_refresh ? "y" : "n")
                      << ", in tRFC post REF: " << (any_ranks_in_trfc ? "y" : "n")
                      << "\n";
#endif
    // Update FAW:
    for (auto& ra : state_)
    {
        while (!ra.faw.empty() && GL_DRAM_CYCLE >= ra.faw.front() + tFAW)
            ra.faw.pop_front();
        if (GL_DRAM_CYCLE >= ra.next_ref_cycle)
        {
            bool preab_issued = try_and_issue_ref(ra, s_refreshes_, s_precharges_);
            if (preab_issued)
            {
                active_buffer_.clear();

                if (in_write_mode_)  // This is the only situation where we are transitioning:
                    update_modal_stats_post_transition();

                in_write_mode_ = false;
                in_transition_ = false;
            }
        }
    }

    // Attempt to switch from read to write mode, or v.v.
    try_switch_to_write_mode();
    if (active_buffer_.empty() && in_transition_)
    {
        update_modal_stats_post_transition();

        in_write_mode_ = !in_write_mode_;
        in_transition_ = false;
    }

    // Try to issue from either the read or write queue:
    issue_next_command();

    // If the write queue is looking empty, then request the LLC to get some writebacks so
    // writes are available when the read queue becomes empty.
    if (write_queue_.size() < virtual_write_queue_watermark_)
        send_demand_writeback_request(GL_LLC);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline bool
trans_add(DRAMChannel::in_queue_type& q, DRAMChannel::pending_type& p, Transaction&& t, size_t qsize)
{
    if (q.size() >= qsize)
        return false;

    p.insert(t.address);
    q.emplace_back(std::move(t));
    return true;
}

bool
DRAMChannel::add_incoming(Transaction t)
{
    // Check for forwarding
    if (pending_writes_.count(t.address))
    {
        if (trans_is_read(t.type))
            outgoing_queue_.emplace(std::move(t), GL_DRAM_CYCLE+1);
        return true;
    }
    // Check for common reads.
    if (trans_is_read(t.type) && pending_reads_.count(t.address))
    {
        auto rd_it = std::find_if(read_queue_.begin(), read_queue_.end(),
                            [addr = t.address] (const auto& x) { return x.trans.address == addr; });
        rd_it->trans.merge(t);
        return true;
    }
    // Add to requisite queue
    if (trans_is_read(t.type))
        return trans_add(read_queue_, pending_reads_, std::move(t), DRAM_RQ_SIZE);
    else
        return trans_add(write_queue_, pending_writes_, std::move(t), DRAM_WQ_SIZE);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
DRAMChannel::deadlock_find_inst(const inst_ptr inst) const
{
    std::cerr << "searching in DRAM channel " << channel_id_ << "...\n";
    auto rd_it = std::find_if(read_queue_.begin(), read_queue_.end(),
                        [&inst] (const auto& x)
                        {
                            const auto& inst_list = x.trans.inst_list;
                            return std::find(inst_list.begin(), inst_list.end(), inst) != inst_list.end();
                        });
    if (rd_it != read_queue_.end())
    {
        size_t rd_pos = std::distance(read_queue_.begin(), rd_it);
        std::cerr << "\tfound in read queue: position = " << rd_pos
                << ", read queue occupancy = " << read_queue_.size()
                << ", write queue occupancy = " << write_queue_.size()
                << ", active buffer occupancy = " << active_buffer_.size()
                << ", in write mode = " << in_write_mode_
                << ", in transition = " << in_transition_ << "\n";
        for (size_t b : active_buffer_)
        {
            auto q_it = std::find_if(write_queue_.begin(), write_queue_.end(),
                                    [b, row=get_bank_const_ref_from_idx(b).open_row.value()] (const auto& x)
                                    {
                                        return b == dram_bank_idx(x.trans.address) 
                                                && row == dram_row(x.trans.address);
                                    });
            if (q_it == write_queue_.end())
                std::cerr << "\tactive buffer entry B" << b << " not in write queue!\n";
            else
            {
                size_t q_pos = std::distance(write_queue_.begin(), q_it);
                std::cerr << "\tactive buffer entry B" << b 
                            << " found in write queue position " << q_pos << "\n";
            }
        }
        return true;
    }
    else
    {
        std::cerr << "\tnothing found\n";
        return false;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::try_switch_to_write_mode()
{
    if (in_write_mode_ || in_transition_)
        return;

    bool drain_cond_1 = write_queue_.size() >= high_watermark_;
    bool drain_cond_2 = !write_queue_.empty() && read_queue_.empty();

    if (!drain_cond_1 && !drain_cond_2)
        return;

    // Setup number of writes per bank:
    const size_t num_writes = write_queue_.size();
    if constexpr (DRAM_WRITE_POLICY == DRAMWritePolicy::SYNC)
    {
        size_t writes_per_bank = OPT_DRAM_WRITE_SYNC_COUNT;
        if (OPT_DRAM_WRITE_SYNC_COUNT == 0)
        {
            writes_per_bank = num_writes / DRAM_TOT_BANKS_PER_CHANNEL;
            writes_per_bank = std::max(static_cast<size_t>(1), writes_per_bank);
        }

        write_budget_per_bank_.fill(OPT_DRAM_WRITE_SYNC_MISS_COST * writes_per_bank);

        // Update the LLC (possibly -- depends on LLC type).
//      update_cache_post_write_drain(GL_LLC, channel_id_, writes_per_bank);
    }
    else
    {
        write_budget_per_bank_.fill(num_writes);

        // Update the LLC (possibly -- depends on LLC type).
//      update_cache_post_write_drain(GL_LLC, channel_id_, num_writes / DRAM_TOT_BANKS_PER_CHANNEL);
    }

    ++s_num_drains_;
    s_tot_read_occu_at_drain_ += read_queue_.size();
    s_tot_write_occu_at_drain_ += write_queue_.size();
    in_transition_ = true;

#if defined(DRAM_RANDOMIZE_WRITE_ADDRESSES)
    // Here, we will fix all the write addresses to the bank indexes in round robin.
    for (size_t i = 0; i < write_queue_.size(); i++)
    {
        auto& trans = write_queue_[i].trans;
        pending_writes_.erase(trans.address);
        trans.address &= ~((DRAM_TOT_BANKS_PER_CHANNEL-1) << BG_OFF);
        trans.address |= fast_mod<DRAM_TOT_BANKS_PER_CHANNEL>(i) << BG_OFF;
        pending_writes_.insert(trans.address);
    }
#endif

#if defined(DRAM_TRACK_ADVANCED_STATS)
    write_counts_array write_cnts{};
    for (const auto& e : write_queue_)
    {
        size_t bank_idx = dram_bank_idx(e.trans.address);
        ++write_cnts[bank_idx];
    }
    dram_update_write_distribution_stats(write_cnts, s_tot_write_queue_std_, s_tot_write_queue_minmax_diff_);
#endif
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::issue_next_command()
{
    auto [ready_cmd, opt_q_entry] = select_ready_command();
    if (cmd_is_invalid(ready_cmd.type))
        return;

    update_dram_state(state_, ready_cmd);

    size_t bank_idx = dram_bank_idx(ready_cmd.address);
    if (cmd_is_cas(ready_cmd.type))
    {
        auto& q_entry = opt_q_entry.value();
        Transaction& trans = q_entry.trans;

        bool is_read = cmd_is_read(ready_cmd.type);
        auto& pending   = is_read ? pending_reads_      : pending_writes_;
        auto& count     = is_read ? s_reads_            : s_writes_;
        auto& row_hits  = is_read ? s_read_row_hits_    : s_write_row_hits_;
        auto& latency   = is_read ? s_tot_read_latency_ : s_tot_write_latency_; 

        pending.erase(trans.address);
        ++count;
        if (q_entry.is_row_buffer_hit)
            ++row_hits;
        if (cmd_is_autopre(ready_cmd.type))
            ++s_precharges_;
        latency += GL_DRAM_CYCLE - q_entry.cycle_entered_queue;

        if (is_read)
        {
            outgoing_queue_.emplace(std::move(trans), GL_DRAM_CYCLE+CL);
            ++s_bank_usage_.reads[bank_idx];
        }
        else
        {
            if constexpr (DRAM_WRITE_POLICY == DRAMWritePolicy::SYNC)
            {
                ssize_t write_cost = q_entry.is_row_buffer_hit ? OPT_DRAM_WRITE_SYNC_HIT_COST 
                                                                  : OPT_DRAM_WRITE_SYNC_MISS_COST;
                write_budget_per_bank_[bank_idx] -= write_cost;
                write_budget_per_bank_[bank_idx] = std::clamp(write_budget_per_bank_[bank_idx], 
                                                                    static_cast<ssize_t>(0),
                                                                    std::numeric_limits<ssize_t>::max());
            }
            ++writes_issued_per_bank_[bank_idx];
            ++s_bank_usage_.writes[bank_idx];
        }
        active_buffer_.erase(bank_idx);
    }
    else 
    {
        if (cmd_is_act(ready_cmd.type))
        {
            ++s_activates_;
            active_buffer_.insert(bank_idx);
        }
        else {
            ++s_precharges_;
            ++s_pre_demand_;
        }
    }
    // additional stats:
#if defined(DRAM_ENABLE_LOGGER)
    tmp_logger_ << "selected command: " << ready_cmd << "\n";
    if (cmd_is_cas(ready_cmd.type))
        dram_logger_ << tmp_logger_.str();
#endif
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

typename DRAMChannel::cmd_output_type
DRAMChannel::select_ready_command()
{
    using bank_ready_cmd_type = std::pair<DRAMCommand, in_queue_type::iterator>;
    using bank_ready_array = std::vector<bank_ready_cmd_type>;

    // Initialize relevant structures:
    auto& q = in_write_mode_ ? write_queue_ : read_queue_;
    SchedulerState algo_state{};

    bank_ready_array bank_ready_cmds(DRAM_TOT_BANKS_PER_CHANNEL, std::make_pair(DRAMCommand(), q.end()));

    bool any_write_is_possible = false;
    for (auto q_it = q.begin(); q_it != q.end(); q_it++)
    {
        DRAMCommand ready_cmd;
        
        size_t bank_idx = dram_bank_idx(q_it->trans.address);
        // Check if bank already has ready command:
        if (bank_ready_cmds[bank_idx].second != q.end())
            continue;

        size_t row = dram_row(q_it->trans.address);
        const auto& b = get_bank_ref_from_idx(bank_idx);

        // If this is a write, check if it violates R->W dependency.
        if (in_write_mode_)
        {
            if (pending_reads_.count(q_it->trans.address))
                continue;

            if constexpr (DRAM_WRITE_POLICY == DRAMWritePolicy::SYNC)
            {
                bool is_write_row_hit = b.open_row.has_value() && b.open_row == row;
                size_t write_cost = is_write_row_hit ? OPT_DRAM_WRITE_SYNC_HIT_COST 
                                                     : OPT_DRAM_WRITE_SYNC_MISS_COST;

                // Check if this write can be completed within the available budget:
                if (write_cost > write_budget_per_bank_[bank_idx])
                    continue;

                // Check if this write's sequence size exceeds the budget:
                if (q_it->trans.dram_sequence_size > write_budget_per_bank_[bank_idx])
                    continue;
            }
        }
        any_write_is_possible = true;

        // Compute the ready command:
        DRAMCommandType type = DRAMCommandType::INVALID;
        if (b.open_row.has_value())
        {
            if (b.open_row == row)
                type = scheduler_get_cas_command(q_it, q, algo_state, b);
            else if (!active_buffer_.count(bank_idx) && scheduler_allow_demand_precharge(q_it, q, algo_state, b))
                type = DRAMCommandType::PRECHARGE;
        }
        else
            type = DRAMCommandType::ACTIVATE;
        ready_cmd = DRAMCommand(q_it->trans.address, type);
        
        // Note that if we are transitioning from reads to writes, we cannot allow any new
        // activates.
        bool cmd_ok = !cmd_is_invalid(type)
                        && (!in_transition_ || !cmd_is_act(type))
                        && cmd_is_issuable(state_, ready_cmd);
        if (cmd_ok)
            bank_ready_cmds[bank_idx] = {ready_cmd, q_it};
        
        // Update algo state:
        algo_state.update_priority(bank_idx, q_it->trans.dram_issue_priority);
    }

    // Rotate the ready command array:
    std::rotate(bank_ready_cmds.begin(),
                bank_ready_cmds.begin() + starting_bank_idx_for_ready_cmd_,
                bank_ready_cmds.end());
    auto cmd_it = std::find_if(bank_ready_cmds.begin(), bank_ready_cmds.end(),
                            [end_it = q.end()] (const auto& x) { return x.second != end_it; });

    // Select ready command amongst all banks:
    DRAMCommand ready_cmd;
    std::optional<RWQueueEntry> q_entry;
    if (cmd_it != bank_ready_cmds.end())
    {
        in_queue_type::iterator q_it = q.end();
        std::tie(ready_cmd, q_it) = *cmd_it;
        if (cmd_is_cas(ready_cmd.type))
        {
            const auto& b = get_bank_ref_from_idx(dram_bank_idx(ready_cmd.address));
            q_it->is_row_buffer_hit = b.next_cas_is_row_buffer_hit;
            q_entry.emplace(std::move(*q_it));
            q.erase(q_it);
        }
        starting_bank_idx_for_ready_cmd_ += std::distance(q.begin(), q_it);
        fast_increment_and_mod_inplace<DRAM_TOT_BANKS_PER_CHANNEL>(starting_bank_idx_for_ready_cmd_);
    }

    // Update state:
    if (in_write_mode_)
    {
        if (!any_write_is_possible)
            in_transition_ = true;
        if constexpr (DRAM_WRITE_POLICY == DRAMWritePolicy::ASYNC)
        {
           if (!read_queue_.empty() && write_queue_.size() < low_watermark_)
                in_transition_ = true;
        }
    }

    return std::make_tuple(ready_cmd, q_entry);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::update_modal_stats_post_transition()
{
    if (in_write_mode_)
    {
        // Update drain latency stats:
        s_tot_drain_latency_ += GL_DRAM_CYCLE - drain_start_cycle_;

#if defined(DRAM_TRACK_ADVANCED_STATS)
        dram_update_write_distribution_stats(writes_issued_per_bank_,
                                                s_tot_write_issue_std_,
                                                s_tot_write_issue_minmax_diff_);
#endif
        end_write_mode(GL_LLC);
        writes_issued_per_bank_.fill(0);
    }
    else
    {
        drain_start_cycle_ = GL_DRAM_CYCLE;
        start_write_mode(GL_LLC);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
DRAMChannel::scheduler_allow_demand_precharge(
        in_queue_type::const_iterator q_it,
        const in_queue_type& q,
        const SchedulerState& s,
        const DRAMBankState& b)
{
    if constexpr (DRAM_SCHED_POLICY == DRAMSchedPolicy::FCFS)
        return true;

    size_t rank_idx = dram_rank(q_it->trans.address);
    size_t bank_idx = dram_bank_idx(q_it->trans.address);

    // Note that this is the same as checking if a cmd is first (as `highest_priority = -128` at the beginning
    // of the queue)
    if (s.highest_priority.at(bank_idx) >= q_it->trans.dram_issue_priority)
        return false;
    
    // For ease of checking, get all commands in the queue that belong to the given bank:
    // We also want to only get commands that fit within some expected sequence budget
    size_t sequence_budget = std::numeric_limits<size_t>::max();
    if (in_write_mode_)
    {
        sequence_budget = write_budget_per_bank_.at(bank_idx);
    }

    std::vector<RWQueueEntry> cmds;
    std::copy_if(std::next(q_it), q.end(), std::back_inserter(cmds),
                    [bank_idx, sequence_budget] (const auto& e)
                    {
                        return dram_bank_idx(e.trans.address) == bank_idx
                                && e.trans.dram_sequence_size <= sequence_budget; 
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

    // Check if we are about to perform a REF:
    if (GL_DRAM_CYCLE >= state_[rank_idx].next_ref_cycle - 32)
        return false;
    
    return true;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMCommandType
DRAMChannel::scheduler_get_cas_command(
        in_queue_type::const_iterator q_it,
        const in_queue_type& q,
        const SchedulerState& s,
        const DRAMBankState& b)
{
    // If we have a closure hint, then use it:
    DRAMClosureHint hint = q_it->trans.dram_closure_hint;

    if constexpr (DRAM_PAGE_POLICY == DRAMPagePolicy::OPEN)
        return in_write_mode_ ? DRAMCommandType::WRITE : DRAMCommandType::READ;
    else if constexpr (DRAM_PAGE_POLICY == DRAMPagePolicy::CLOSE)
        return in_write_mode_ ? DRAMCommandType::WRITE_PRECHARGE : DRAMCommandType::READ_PRECHARGE;
    else
    {
        DRAMCommandType cmd =         in_write_mode_ ? DRAMCommandType::WRITE 
                                                     : DRAMCommandType::READ,
                        cmd_autopre = in_write_mode_ ? DRAMCommandType::WRITE_PRECHARGE 
                                                     : DRAMCommandType::READ_PRECHARGE;

        size_t bank_idx = dram_bank_idx(q_it->trans.address);

        // If we are in transition, we will have no more row buffer hits:
        if (in_transition_)
            return cmd_autopre;

        // Predict whether or not we will transition any time soon:
        constexpr size_t TOL = 4;
        if (in_write_mode_)
        {
            if (!read_queue_.empty() && (write_queue_.size() <= low_watermark_ + TOL))
                return cmd_autopre;
            if (write_budget_per_bank_[bank_idx] <= OPT_DRAM_WRITE_SYNC_MISS_COST)
                return cmd_autopre;
        }
        if (!in_write_mode_ && (read_queue_.size() < TOL || write_queue_.size() >= high_watermark_ - TOL))
            return cmd_autopre;

        // For ease of checking, get all commands in the queue that belong to the given bank:
        std::vector<RWQueueEntry> cmds;
        std::copy_if(std::next(q_it), q.end(), std::back_inserter(cmds),
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

        if (hint == DRAMClosureHint::CLOSE_AFTER)
            return cmd_autopre;
        else
            return cmd;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

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

constexpr DRAMCommandType READ_CMD = (DRAM_PAGE_POLICY == DRAMPagePolicy::OPEN)
                                        ? DRAMCommandType::READ
                                        : DRAMCommandType::READ_PRECHARGE;

constexpr DRAMCommandType WRITE_CMD = (DRAM_PAGE_POLICY == DRAMPagePolicy::OPEN)
                                        ? DRAMCommandType::WRITE
                                        : DRAMCommandType::WRITE_PRECHARGE;

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
{}

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
        << "========================= DRAM CYCLE " << GL_DRAM_CYCLE << " ===============================\n"
        << "channel state: FAW = {";

    for (uint64_t c : state_.faw)
        tmp_logger_ << " " << ((c+tFAW) - GL_DRAM_CYCLE);

    tmp_logger_ << " }, in REF: " << (any_ranks_in_refresh ? "y" : "n")
                      << ", in tRFC post REF: " << (any_ranks_in_trfc ? "y" : "n")
                      << "\n";
#endif
    // Update FAW:
    while (!state_.faw.empty() && GL_DRAM_CYCLE >= state_.faw.front() + tFAW)
        state_.faw.pop_front();

    // Handle refresh if any rank needs it.
    auto ra_it = std::find_if(state_.begin(), state_.end(), 
                        [] (const auto& ra)
                        {
                            return GL_DRAM_CYCLE >= ra.next_ref_cycle;
                        });
    if (ra_it != state_.end())
        try_and_issue_ref(*ra_it, s_refreshes_, s_precharges_);
    else
    {
        try_switch_to_write_mode();
        issue_next_command();
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline bool
trans_add(DRAMChannel::in_queue_type& q, DRAMChannel::pending_type& p, Transaction&& t, size_t qsize)
{
    if (q.size() >= qsize)
        return false;
    ++p[t.address];
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
            outgoing_queue_.emplace(t, GL_DRAM_CYCLE);
        return true;
    }
    // Check for common reads.
    if (trans_is_read(t.type) && pending_reads_.count(t.address))
    {
        auto rd_it = std::find_if(read_queue_.begin(), read_queue_.end(),
                            [addr = t.address] (const auto& x)
                            {
                                return x.trans.address == addr;
                            });
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

typename DRAMChannel::cmd_output_type
DRAMChannel::select_ready_command()
{
    auto& q = (tot_writes_to_drain_ > 0) ? write_queue_ : read_queue_;
        
    DRAMCommand ready_cmd;
    std::optional<RWQueueEntry> q_entry;

    SchedulerState algo_state{};
    algo_state.is_write_mode = tot_writes_to_drain_ > 0;

    bool any_write_is_possible = false;
    for (auto q_it = q.begin(); q_it != q.end(); q_it++)
    {
        size_t bank_idx = dram_bank_idx(q_it->trans.address);
        size_t row = dram_row(q_it->trans.address);

        // If this is a write, check if it violates R->W dependency.
        if (tot_writes_to_drain_ > 0)
        {
            if (pending_reads_.count(q_it->trans.address))
            {
                // Exit write mode
                tot_writes_to_drain_ = 0;
                writes_to_drain_per_bank_.fill(0);
                break;
            }
            if (writes_to_drain_per_bank_[bank_idx] == 0)
                continue;
        }
        any_write_is_possible = true;
        // Otherwise, compute the ready command:
        const auto& b = get_bank_ref_from_idx(bank_idx);
        DRAMCommandType type = DRAMCommandType::INVALID;
        if (b.open_row.has_value())
        {
            if (b.open_row == row)
                type = scheduler_get_cas_command(q_it, q, algo_state, b);
            else if (scheduler_allow_demand_precharge(q_it, q, algo_state, b))
                type = DRAMCommandType::PRECHARGE;
        }
        else
            type = DRAMCommandType::ACTIVATE;
        ready_cmd = DRAMCommand(q_it->trans.address, type);

        if (!cmd_is_invalid(type) && cmd_is_issuable(state_, ready_cmd))
        {
            if (cmd_is_cas(type))
            {
                q_it->is_row_buffer_hit = b.next_cas_is_row_buffer_hit;
                q_entry.emplace(std::move(*q_it));
                q.erase(q_it);
            }
            break;
        }
        else
            ready_cmd.type = DRAMCommandType::INVALID;
        algo_state.is_first[bank_idx] = false;
    }

    if (!any_write_is_possible)
    {
        tot_writes_to_drain_ = 0;
        writes_to_drain_per_bank_.fill(0);
    }
    return std::make_tuple(ready_cmd, q_entry);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::try_switch_to_write_mode()
{
    if (tot_writes_to_drain_ == 0)
    {
#if defined(DRAM_USE_WATERMARKS_TO_DRAIN)
        bool drain_cond_1 = write_queue_.size() >= high_watermark_;
        bool drain_cond_2 = write_queue_.size() > low_watermark_
                                && read_queue_.empty();
#else
        bool drain_cond_1 = write_queue_.size() >= DRAM_WQ_SIZE,
             drain_cond_2 = read_queue_.empty()
                            && write_queue_.size() > 8;
#endif
        if (drain_cond_1 || drain_cond_2)
        {
            size_t num_writes = write_queue_.size() - low_watermark_;
            size_t writes_per_bank = num_writes / DRAM_TOT_BANKS_PER_CHANNEL;
            if constexpr (DRAM_WRITE_POLICY == DRAMWritePolicy::SYNC)
            {
                // Reocompute `writes_per_bank` accordingly:
                if (OPT_DRAM_WRITE_SYNC_COUNT == 0)
                    writes_per_bank = std::max(static_cast<size_t>(1), writes_per_bank);
                else
                    writes_per_bank = OPT_DRAM_WRITE_SYNC_COUNT;

                size_t max_writes = writes_per_bank * DRAM_TOT_BANKS_PER_CHANNEL;
                size_t write_cost = (2*s_write_row_hits_ < s_writes_)
                                    ? OPT_DRAM_WRITE_SYNC_MISS_COST : OPT_DRAM_WRITE_SYNC_HIT_COST;

                writes_to_drain_per_bank_.fill(write_cost * writes_per_bank);
                tot_writes_to_drain_ = std::min(num_writes, max_writes);
            }
            else
            {
                writes_to_drain_per_bank_.fill(num_writes);
                tot_writes_to_drain_ = num_writes;
            }
            ++s_num_drains_;
            s_tot_read_occu_at_drain_ += read_queue_.size();

            GL_LLC->sig_dram_write_drain(channel_id_, writes_per_bank);

#if defined(DRAM_TRACK_ADVANCED_STATS)
            std::array<size_t, DRAM_TOT_BANKS_PER_CHANNEL> write_cnts{};
            for (const auto& e : write_queue_)
            {
                size_t bank_idx = dram_bank_idx(e.trans.address);
                ++write_cnts[bank_idx];
            }
            double mean_writes = static_cast<double>(write_queue_.size()) 
                                    / static_cast<double>(DRAM_TOT_BANKS_PER_CHANNEL);
#define SQR(x) (x)*(x)
            double variance = std::transform_reduce(write_cnts.begin(), write_cnts.end(), 0.0,
                                    std::plus<double>{},
                                    [mean_writes] (size_t x)
                                    {
                                        return SQR(static_cast<double>(x) - mean_writes); 
                                    }) / static_cast<double>(DRAM_TOT_BANKS_PER_CHANNEL);
            s_tot_write_variance_ += variance;
#undef SQR
#endif
        }
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline void
dec_pending(DRAMChannel::pending_type& m, uint64_t k)
{
    if ((--m[k]) == 0)
        m.erase(k);
}

void
DRAMChannel::issue_next_command()
{
    auto [ready_cmd, opt_q_entry] = select_ready_command();
    if (cmd_is_invalid(ready_cmd.type))
        return;
    update_dram_state(state_, ready_cmd);
    if (cmd_is_cas(ready_cmd.type))
    {
        auto& q_entry = opt_q_entry.value();
        Transaction& trans = q_entry.trans;

        bool is_read = cmd_is_read(ready_cmd.type);
        auto& pending   = is_read ? pending_reads_      : pending_writes_;
        auto& count     = is_read ? s_reads_            : s_writes_;
        auto& row_hits  = is_read ? s_read_row_hits_    : s_write_row_hits_;
        auto& latency   = is_read ? s_tot_read_latency_ : s_tot_write_latency_; 

        dec_pending(pending, trans.address);
        ++count;
        if (q_entry.is_row_buffer_hit)
            ++row_hits;
        if (cmd_is_autopre(ready_cmd.type))
            ++s_precharges_;
        latency += GL_DRAM_CYCLE - q_entry.cycle_entered_queue;

        if (is_read)
            outgoing_queue_.emplace(std::move(trans), GL_DRAM_CYCLE + CL);
        else
        {
            --tot_writes_to_drain_;
            
            if constexpr (DRAM_WRITE_POLICY == DRAMWritePolicy::SYNC)
            {
                size_t bank_idx = dram_bank_idx(trans.address);
                size_t write_cost = q_entry.is_row_buffer_hit ? OPT_DRAM_WRITE_SYNC_HIT_COST 
                                                                  : OPT_DRAM_WRITE_SYNC_MISS_COST;
                clampsub(writes_to_drain_per_bank_[bank_idx], write_cost);
            }
        }
    }
    else 
    {
        if (cmd_is_act(ready_cmd.type))
            ++s_activates_;
        else {
            ++s_precharges_;
            ++s_pre_demand_;
        }
    }
    // additional stats:
#if defined(DRAM_TRACK_ADVANCED_STATS)
    update_wrw_state(ready_cmd);
#endif

#if defined(DRAM_ENABLE_LOGGER)
    tmp_logger_ << "selected command: " << ready_cmd << "\n";
    if (cmd_is_cas(ready_cmd.type))
        dram_logger_ << tmp_logger_.str();
#endif
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::update_wrw_state(const DRAMCommand& ready_cmd)
{
    if (!cmd_is_cas(ready_cmd.type))
        return;
    // Current logger setup: we only write to `dram_logger_` if the sequence is of the form:
    //  (1) a write
    //  (2) one or more reads
    //  (3) a write
    switch (wrw_seq_state_)
    {
    case WRWSequenceState::NEED_WRITE:
        if (cmd_is_write(ready_cmd.type))
        {
            wrw_seq_state_ = WRWSequenceState::NEED_READ;
            wrw_first_write_cycle_ = GL_DRAM_CYCLE;
        }
        break;
    case WRWSequenceState::NEED_READ:
        if (cmd_is_write(ready_cmd.type))
            wrw_first_write_cycle_ = GL_DRAM_CYCLE;
        else
            wrw_seq_state_ = WRWSequenceState::IN_READS;
        break;
    case WRWSequenceState::IN_READS:
        // Can only promote in this state.
        if (cmd_is_write(ready_cmd.type))
        {
            wrw_seq_state_ = WRWSequenceState::NEED_READ;
            for (size_t i = 0; i < 4; i++)
            {
                uint64_t max_cyc = 1L << (i+8);
                if (GL_DRAM_CYCLE - wrw_first_write_cycle_ <= max_cyc)
                    ++s_num_seq_[i];
            }
            wrw_first_write_cycle_ = GL_DRAM_CYCLE;
        }
        else if (GL_DRAM_CYCLE - wrw_first_write_cycle_ > 2048)
            // Reset the state as we are taking too long to reach the next write.
            wrw_seq_state_ = WRWSequenceState::NEED_WRITE;
        break;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

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
{
    cmd_scheduler_ = cmd_sch_ptr(new CommandScheduler(state_));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::tick_mc()
{
    try_switch_to_write_mode();

    auto& q = tot_writes_to_drain_ ? write_queue_ : read_queue_;
    auto it = std::find_if(q.begin(), q.end(),
                    [this, write_mode=(tot_writes_to_drain_>0)] (const Transaction& t)
                    {
                        if (write_mode)
                        {
                            if (this->pending_reads_.count(t.address))
                                return false;
                            if (this->writes_to_drain_per_bank_[ get_bank_idx(t.address) ] == 0)
                                return false;
                        }
                        return this->cmd_scheduler_->can_accept(t.address, write_mode);
                    });
    if (it != q.end())
    {
        DRAMCommandType cmd_type = READ_CMD;
        if (tot_writes_to_drain_ > 0)
        {
            // Check if this transaction has any hints.
            if (it->dram_write_hint_valid)
                cmd_type = it->dram_write_hint_do_autopre 
                            ? DRAMCommandType::WRITE_PRECHARGE : DRAMCommandType::WRITE;
            else
                cmd_type = WRITE_CMD;

            if constexpr (DRAM_WRITE_POLICY == DRAMWritePolicy::SYNC)
            {
                uint64_t address = it->address;
                size_t bank_idx = get_bank_idx(address);
                size_t row = dram_row(address);

                bool is_row_buffer_hit = !cmd_is_autopre(cmd_type)
                                         && std::any_of(std::next(it), q.end(),
                                                [bank_idx, row] (const Transaction& t)
                                                {
                                                    return get_bank_idx(t.address) == bank_idx
                                                            && dram_row(t.address) == row;
                                                });
                size_t write_cost = is_row_buffer_hit ? OPT_DRAM_WRITE_SYNC_HIT_COST : OPT_DRAM_WRITE_SYNC_MISS_COST;
                clampsub(writes_to_drain_per_bank_[bank_idx], write_cost);
            }
            --tot_writes_to_drain_;
        } 
        cmd_scheduler_->enqueue(std::move(*it), cmd_type);
        q.erase(it);
    }
    else
    {
        tot_writes_to_drain_ = 0;
        writes_to_drain_per_bank_.fill(0);
    }
}

void
DRAMChannel::tick_dram()
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
        issue_next_cmd();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline bool
trans_add(DRAMChannel::in_queue_type& q, DRAMChannel::pending_type& p, Transaction&& t, size_t qsize)
{
    if (q.size() >= qsize)
        return false;
    ++p[t.address];
    q.push_back(std::move(t));
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
                            [addr = t.address] (const Transaction& x)
                            {
                                return x.address == addr;
                            });
        if (rd_it != read_queue_.end())
        {
            rd_it->merge(t);
            return true;
        }
    }
    // Add to requisite queue
    if (trans_is_read(t.type))
        return trans_add(read_queue_, pending_reads_, std::move(t), DRAM_RQ_SIZE);
    else
        return trans_add(write_queue_, pending_writes_, std::move(t), DRAM_WQ_SIZE);
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
                                && read_queue_.empty()
                                && cmd_scheduler_->has_no_pending_reads();
#else
        bool drain_cond_1 = write_queue_.size() >= DRAM_WQ_SIZE,
             drain_cond_2 = read_queue_.empty()
                            && write_queue_.size() > 8
                            && cmd_scheduler_->has_no_pending_reads();
#endif
        if (drain_cond_1 || drain_cond_2)
        {
            size_t num_writes = write_queue_.size() - low_watermark_;
            size_t writes_per_bank;
            if constexpr (DRAM_WRITE_POLICY == DRAMWritePolicy::SYNC)
            {
                if (OPT_DRAM_WRITE_SYNC_COUNT == 0)
                {
                    size_t mean_writes_per_bank = num_writes / DRAM_TOT_BANKS_PER_CHANNEL;
                    writes_per_bank = std::max(static_cast<size_t>(1), mean_writes_per_bank);
                }
                else
                    writes_per_bank = OPT_DRAM_WRITE_SYNC_COUNT;

                writes_per_bank -= read_queue_.size()
                                    / (DRAM_TOT_BANKS_PER_CHANNEL*OPT_DRAM_WRITE_SYNC_READ_DIVISOR);

                size_t max_writes = writes_per_bank * DRAM_TOT_BANKS_PER_CHANNEL;
                size_t write_cost = (2*s_write_row_hits_ < s_writes_)
                                    ? OPT_DRAM_WRITE_SYNC_MISS_COST : OPT_DRAM_WRITE_SYNC_HIT_COST;

                writes_to_drain_per_bank_.fill(write_cost * writes_per_bank);
                tot_writes_to_drain_ = std::min(num_writes, max_writes);
                GL_LLC->sig_dram_write_drain(channel_id_, writes_per_bank);
            }
            else
            {
                writes_to_drain_per_bank_.fill(num_writes);
                tot_writes_to_drain_ = num_writes;

                GL_LLC->sig_dram_write_drain(channel_id_, num_writes / DRAM_TOT_BANKS_PER_CHANNEL);
            }
            ++s_num_drains_;

#if defined(DRAM_TRACK_ADVANCED_STATS)
            std::array<size_t, DRAM_TOT_BANKS_PER_CHANNEL> write_cnts{};
            for (const auto& t : write_queue_)
            {
                size_t bank_idx = get_bank_idx(t.address);
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
DRAMChannel::issue_next_cmd()
{
#if defined(DRAM_ENABLE_LOGGER)
    cmd_scheduler_->print_queue_state(tmp_logger_);
#endif

    auto [ready_cmd, opt_q_entry] = cmd_scheduler_->select_command();
    if (cmd_is_invalid(ready_cmd.type))
        return;
    update_dram_state(state_, ready_cmd);
    if (cmd_is_cas(ready_cmd.type))
    {
        auto q_entry = opt_q_entry.value();
        Transaction& trans = q_entry.trans;

        uint64_t latency = GL_DRAM_CYCLE - q_entry.cycle_entered_queue;
        if (cmd_is_read(ready_cmd.type))
        {
            dec_pending(pending_reads_, trans.address);
            ++s_reads_;
            if (q_entry.is_row_buffer_hit)
                ++s_read_row_hits_;
            if (cmd_is_autopre(ready_cmd.type))
                ++s_precharges_;
            s_tot_read_latency_ += latency;
            // Mark as outgoing.
            outgoing_queue_.emplace(std::move(trans), GL_DRAM_CYCLE + CL);
        } 
        else
        {
            dec_pending(pending_writes_, trans.address);
            // Update stats.
            ++s_writes_;
            if (q_entry.is_row_buffer_hit)
                ++s_write_row_hits_;
            if (cmd_is_autopre(ready_cmd.type))
                ++s_precharges_;
            s_tot_write_latency_ += latency;
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

/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#include "globals.h"
#include "dram_timing.h"

#include "dram/address.h"
#include "dram/channel.h"
#include "dram/cmd_queue.h"
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

    auto& q = writes_to_drain_ > 0 ? write_queue_ : read_queue_;
    auto it = std::find_if(q.begin(), q.end(),
                    [this, write_mode=(writes_to_drain_>0)] (const Transaction& t)
                    {
                        if (write_mode && this->pending_reads_.count(t.address))
                            return false;
                        return this->cmd_scheduler_->can_accept(t.address, write_mode);
                    });
    if (it != q.end()) {
        DRAMCommandType cmd_type = READ_CMD;
        if (writes_to_drain_ > 0)
        {
            cmd_type = WRITE_CMD;
            --writes_to_drain_;
        } 
        cmd_scheduler_->enqueue(std::move(*it), cmd_type);
        q.erase(it);
    }
}

void
DRAMChannel::tick_dram()
{
#if defined(DRAM_ENABLE_LOGGER)
    tmp_logger_local_.str("");

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

    tmp_logger_local_ 
        << "========================= DRAM CYCLE " << GL_DRAM_CYCLE << " ===============================\n"
        << "channel state: FAW = {";

    for (uint64_t c : state_.faw)
        tmp_logger_local_ << " " << ((c+tFAW) - GL_DRAM_CYCLE);

    tmp_logger_local_ << " }, in REF: " << (any_ranks_in_refresh ? "y" : "n")
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

    // Issue commands from the cmd queue.
    issue_next_cmd();
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline bool
trans_add(DRAMChannel::in_queue_t& q, DRAMChannel::pending_t& p, Transaction t, size_t qsize)
{
    if (q.size() >= qsize)
        return false;
    q.push_back(t);
    ++p[t.address];
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
        if (rd_it == read_queue_.end())
        {
            rd_it->merge(t);
            return true;
        }
    }
    // Add to requisite queue
    if (trans_is_read(t.type))
        return trans_add(read_queue_, pending_reads_, t, DRAM_RQ_SIZE);
    else
        return trans_add(write_queue_, pending_writes_, t, DRAM_WQ_SIZE);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::try_switch_to_write_mode()
{
    if (LLCache::WRITEBACK_MODE == CacheWbMode::VIRTUAL_WRITE_QUEUE)
        return; // Switch is performed by the LLC instead of MC.

    if (writes_to_drain_ == 0)
    {
#if defined(DRAM_USE_WATERMARKS_TO_DRAIN)
        bool drain_cond_1 = write_queue_.size() >= DRAM_HIGH_WATERMARK;
        bool drain_cond_2 = write_queue_.size() > DRAM_LOW_WATERMARK
                                && read_queue_.empty()
                                && cmd_scheduler_->has_no_pending_reads();
        if (drain_cond_1 || drain_cond_2)
            writes_to_drain_ = write_queue_.size() - DRAM_LOW_WATERMARK;
#else
        bool drain_cond_1 = write_queue_.size() == DRAM_WQ_SIZE,
             drain_cond_2 = read_queue_.empty()
                            && write_queue_.size() > 8
                            && cmd_scheduler_->has_no_pending_reads();
        if (drain_cond_1 || drain_cond_2)
            writes_to_drain_ = write_queue_.size();
#endif
#if defined(DRAM_TRACK_ADVANCED_STATS)
        if (drain_cond_1)
        {
            // Demand drain: we are interested in the spread of writes across bankgroups.
            std::array<size_t, DRAM_RANKS*DRAM_BANKGROUPS> cnt{};
            for (const Transaction& t : write_queue_)
            {
                size_t ii = dram_bankgroup(t.address) + dram_rank(t.address)*DRAM_BANKGROUPS;
                ++cnt[ii];
            }
            // Update stats
            const auto& [min_it, max_it] = std::minmax_element(cnt.begin(), cnt.end());
            s_tot_drain_bg_spread_ += (*max_it) - (*min_it);

            ++s_num_drains_;
        }
#endif
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline void
dec_pending(DRAMChannel::pending_t& m, uint64_t k)
{
    if ((--m[k]) == 0)
        m.erase(k);
}

void
DRAMChannel::issue_next_cmd()
{
#if defined(DRAM_ENABLE_LOGGER)
    cmd_scheduler_->print_queue_state(tmp_logger_local_);
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

#if defined(DRAM_ENABLE_LOGGER)
    tmp_logger_local_ << "selected command: " << ready_cmd << "\n";
#if defined(DRAM_LOG_WRW_SEQUENCES)
    log_write_read_write_sequence(ready_cmd);
#else
    if (cmd_is_cas(ready_cmd.type))
        dram_logger_ << tmp_logger_local_.str();
#endif
#endif
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::log_write_read_write_sequence(const DRAMCommand& ready_cmd)
{
    if (!cmd_is_cas(ready_cmd.type))
        return;
    // Current logger setup: we only write to `dram_logger_` if the sequence is of the form:
    //  (1) a write
    //  (2) one or more reads
    //  (3) a write
    switch (logger_state_)
    {
    case LoggerCmdState::NEED_WRITE:
        if (cmd_is_write(ready_cmd.type))
        {
            logger_state_ = LoggerCmdState::NEED_READ;
            logger_first_write_cycle_ = GL_DRAM_CYCLE;
            tmp_logger_global_ << tmp_logger_local_.str();
        }
        break;
    case LoggerCmdState::NEED_READ:
        if (cmd_is_write(ready_cmd.type))
        {
            // As this is a write, stay in the same state, but update `tmp_logger_global_`
            logger_first_write_cycle_ = GL_DRAM_CYCLE;
            tmp_logger_global_.str("");
            tmp_logger_global_ << tmp_logger_local_.str();
        }
        else
        {
            logger_state_ = LoggerCmdState::IN_READS;
            tmp_logger_global_ << tmp_logger_local_.str();
        }
        break;
    case LoggerCmdState::IN_READS:
        // Can only promote in this state.
        tmp_logger_global_ << tmp_logger_local_.str();
        if (cmd_is_write(ready_cmd.type))
        {
            // We are done: write to dram_logger_
            dram_logger_ << "SEQUENCE START\n\n" 
                         << tmp_logger_global_.str() 
                         << "\nSEQUENCE END (t = " << (GL_DRAM_CYCLE-logger_first_write_cycle_) << "\n";
            tmp_logger_global_.str(tmp_logger_local_.str());

            logger_state_ = LoggerCmdState::NEED_READ;
            logger_first_write_cycle_ = GL_DRAM_CYCLE;
        }
        else if (GL_DRAM_CYCLE - logger_first_write_cycle_ > 2048)
        {
            // Reset the state as we are taking too long to reach the next write.
            logger_state_ = LoggerCmdState::NEED_WRITE;
            tmp_logger_global_.str("");
        }
        break;
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

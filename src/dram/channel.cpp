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
#include "dram/stats.h"

#include <iomanip>

#define DRAM_ENABLE_LOGGER

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMChannel::DRAMChannel(size_t channel_id, double freq_ghz)
    :freq_ghz_(freq_ghz),
    channel_id_(channel_id),
    scheduler_(new scheduler_impl(this, state_)),
    virtual_write_queue_watermark_(DRAM_WQ_SIZE * OPT_DRAM_HIGH_WATERMARK * 0.8),
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
                scheduler_->handle_preab_forced_transition();
        }
    }

    scheduler_->update_state();

    issue_next_command();

    // If the write queue is looking empty, then request the LLC to get some writebacks so
    // writes are available when the read queue becomes empty.
    if (scheduler_->write_occu() < virtual_write_queue_watermark_)
        cache_send_demand_writeback_request(GL_LLC);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

bool
DRAMChannel::deadlock_find_inst(const inst_ptr inst) const
{
    std::cerr << "searching in DRAM channel " << channel_id_ << "...\n";

    // Search in outgoing queue:

    // Search for instruction in scheduler's R/W queues:
    if (scheduler_->deadlock_find_inst(inst))
        return true;

    std::cerr << "\tnothing found\n";
    return false;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::issue_next_command()
{
    auto [ready_cmd, opt_q_entry] = scheduler_->select_ready_command();
    if (ready_cmd.is_invalid())
        return;

#if defined(DRAM_ENABLE_LOGGER)
    tmp_logger_ << "[ " << GL_DRAM_CYCLE << " ] selected command: " << ready_cmd;
#endif

    update_dram_state(state_, ready_cmd);

    size_t bank_idx = dram_bank_idx(ready_cmd.address);
    if (ready_cmd.is_cas())
    {
        auto& q_entry = opt_q_entry.value();
        Transaction& trans = q_entry.trans;

        auto& b = channel_get_bank_ref_from_idx(state_, dram_bank_idx(trans.address));

        const bool is_read = ready_cmd.is_read();
        auto& count     = is_read ? s_reads_            : s_writes_;
        auto& row_hits  = is_read ? s_read_row_hits_    : s_write_row_hits_;
        auto& latency   = is_read ? s_tot_read_latency_ : s_tot_write_latency_; 

        ++count;
        if (q_entry.is_row_buffer_hit)
        {
            ++row_hits;
#if defined(DRAM_ENABLE_LOGGER)
            tmp_logger_ << std::setw(16) << std::left << "\tis row buffer hit";
#endif
        }
        else
        {
#if defined(DRAM_ENABLE_LOGGER)
            tmp_logger_ << std::setw(16) << std::left << "\tis NOT row buffer hit";
#endif
        }
#if defined(DRAM_ENABLE_LOGGER)
        tmp_logger_ << "\t+" << std::setw(5) << std::left << (GL_DRAM_CYCLE - logger_last_cas_cycle_);
    
        if (q_entry.trans.is_write())
            tmp_logger_ << "\tis demand writeback = " << q_entry.trans.dram_is_demand_writeback;

        logger_last_cas_cycle_ = GL_DRAM_CYCLE;
#endif
        b.last_access_cycle = GL_DRAM_CYCLE;

        if (ready_cmd.autopre)
            ++s_precharges_;
        latency += GL_DRAM_CYCLE - q_entry.cycle_entered_queue;

        if (is_read)
        {
            outgoing_queue_.emplace(std::move(trans), GL_DRAM_CYCLE + CL + DRAM_BURST_LENGTH/2);
            ++s_bank_usage_.reads[bank_idx];
        }
        else
        {
            ++writes_issued_per_bank_[bank_idx];
            ++s_bank_usage_.writes[bank_idx];
        }
    }
    else if (ready_cmd.is_act())
    {
        ++s_activates_;
    }
    else 
    {
        ++s_precharges_;
        ++s_pre_demand_;
    }
    // additional stats:
#if defined(DRAM_ENABLE_LOGGER)
    if (ready_cmd.is_cas())
        dram_logger_ << tmp_logger_.str() << "\n";
#endif
}
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::update_modal_stats_post_transition()
{
    if (scheduler_->is_in_write_mode())
    {
        drain_start_cycle_ = GL_DRAM_CYCLE;

        s_tot_read_occu_at_drain_ += scheduler_->read_occu();
        s_tot_write_occu_at_drain_ += scheduler_->write_occu();
        ++s_num_drains_;
        if (scheduler_->write_occu() >= scheduler_->high_watermark_ || scheduler_->any_write_queues_full())
            ++s_num_forced_drains_;

        writes_issued_per_bank_.fill(0);

#if defined(DRAM_ENABLE_LOGGER)
        dram_logger_ << "----------- WRITE MODE START ------------ CYCLE = " << GL_DRAM_CYCLE << "\n";
#endif
    }
    else
    {
        // Update drain latency stats:
        s_tot_drain_latency_ += GL_DRAM_CYCLE - drain_start_cycle_;

#if defined(DRAM_TRACK_ADVANCED_STATS)
        dram_update_write_distribution_stats(writes_issued_per_bank_,
                                                s_tot_write_issue_std_,
                                                s_tot_write_issue_minmax_diff_);
#endif

        size_t tot_writes = std::reduce(writes_issued_per_bank_.begin(), writes_issued_per_bank_.end(), 0);

        writes_issued_per_bank_.fill(0);
        
#if defined(DRAM_ENABLE_LOGGER)
        dram_logger_ << "----------- WRITE MODE END ------------ CYCLE = " << GL_DRAM_CYCLE
            << "\tCOUNT = " << tot_writes
            << "\tTIME IN DRAIN = " << (GL_DRAM_CYCLE - drain_start_cycle_) << "\n";
#endif
    }

    cache_toggle_write_mode(GL_LLC, scheduler_->is_in_write_mode());
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

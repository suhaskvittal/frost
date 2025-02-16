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

//#define DRAM_ENABLE_LOGGER

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

DRAMChannel::DRAMChannel(size_t channel_id, double freq_ghz)
    :freq_ghz_(freq_ghz),
    channel_id_(channel_id),
#if defined(DRAM_ENABLE_LOGGER)
    dram_logger_("dram_channel." + std::to_string(channel_id) + ".log")
#else
    dram_logger_(),
    scheduler_(new scheduler_impl(this, state_))
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
                scheduler_->handle_preab_forced_transition();
        }
    }

    scheduler_->update_state();
    issue_next_command();

    // If the write queue is looking empty, then request the LLC to get some writebacks so
    // writes are available when the read queue becomes empty.
    if (scheduler_->write_occu() < virtual_write_queue_watermark_)
        send_demand_writeback_request(GL_LLC);
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
    if (cmd_is_invalid(ready_cmd.type))
        return;

    update_dram_state(state_, ready_cmd);

    size_t bank_idx = dram_bank_idx(ready_cmd.address);
    if (cmd_is_cas(ready_cmd.type))
    {
        auto& q_entry = opt_q_entry.value();
        Transaction& trans = q_entry.trans;

        bool is_read = cmd_is_read(ready_cmd.type);
        auto& count     = is_read ? s_reads_            : s_writes_;
        auto& row_hits  = is_read ? s_read_row_hits_    : s_write_row_hits_;
        auto& latency   = is_read ? s_tot_read_latency_ : s_tot_write_latency_; 

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
            ++writes_issued_per_bank_[bank_idx];
            ++s_bank_usage_.writes[bank_idx];
        }
    }
    else if (cmd_is_act(ready_cmd.type))
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
    tmp_logger_ << "selected command: " << ready_cmd << "\n";
    if (cmd_is_cas(ready_cmd.type))
        dram_logger_ << tmp_logger_.str();
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

        start_write_mode(GL_LLC);
        writes_issued_per_bank_.fill(0);
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
        end_write_mode(GL_LLC);
        writes_issued_per_bank_.fill(0);
    }
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

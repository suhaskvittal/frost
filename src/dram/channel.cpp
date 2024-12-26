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

DRAMChannel::DRAMChannel(double freq_ghz)
    :freq_ghz_(freq_ghz)
{
    cmd_scheduler_ = cmd_sch_ptr(new CommandScheduler(state_));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void
DRAMChannel::tick_mc()
{
    // Here, we will just schedule the next command from the read/write queues.
    //
    // Determine if we need to drain writes.
    if (writes_to_drain_ == 0)
    {
        bool drain_cond_1 = write_queue_.size() == DRAM_WQ_SIZE,
             drain_cond_2 = read_queue_.empty()
                            && write_queue_.size() > 8
                            && cmd_scheduler_->has_no_pending_reads();
        if (drain_cond_1 || drain_cond_2)
            writes_to_drain_ = write_queue_.size();
    }

    auto& q = writes_to_drain_ > 0 ? write_queue_ : read_queue_;
    auto it = std::find_if(q.begin(), q.end(),
                    [this, write_mode=(writes_to_drain_>0)] (const Transaction& t)
                    {
                        if (write_mode && this->pending_reads_.count(t.address))
                            return false;
                        return this->cmd_scheduler_->can_accept(t.address, write_mode);
                    });
    if (it != q.end()) {
        if (writes_to_drain_ > 0)
        {
            cmd_scheduler_->enqueue(DRAMCommand(*it, WRITE_CMD));
            --writes_to_drain_;
        } 
        else
            cmd_scheduler_->enqueue(DRAMCommand(*it, READ_CMD));
        q.erase(it);
    }
}

void
DRAMChannel::tick_dram()
{
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

inline void
dec_pending(DRAMChannel::pending_t& m, uint64_t k)
{
    if ((--m[k]) == 0)
        m.erase(k);
}

void
DRAMChannel::issue_next_cmd()
{
    DRAMCommand ready_cmd = cmd_scheduler_->select_command();

    if (cmd_is_invalid(ready_cmd.type))
        return;

    update_dram_state(state_, ready_cmd);

    if (cmd_is_read(ready_cmd.type))
    {
        // Mark as outgoing.
        outgoing_queue_.emplace(ready_cmd.trans, GL_DRAM_CYCLE + CL);
        dec_pending(pending_reads_, ready_cmd.trans.address);
        ++s_reads_;
        if (ready_cmd.is_row_buffer_hit)
            ++s_read_row_hits_;
        if (cmd_is_autopre(ready_cmd.type))
            ++s_precharges_;
    } 
    else if (cmd_is_write(ready_cmd.type)) 
    {
        dec_pending(pending_writes_, ready_cmd.trans.address);
        // Update stats.
        ++s_writes_;
        if (ready_cmd.is_row_buffer_hit)
            ++s_write_row_hits_;
        if (cmd_is_autopre(ready_cmd.type))
            ++s_precharges_;
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
}
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

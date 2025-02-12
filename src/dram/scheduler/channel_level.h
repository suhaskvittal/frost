/*
 *  author: Suhas Vittal
 *  date:   11 February 2025
 * */

#ifndef DRAM_SCHEDULER_CHANNEL_LEVEL_h
#define DRAM_SCHEDULER_CHANNEL_LEVEL_h

#include "dram/scheduler/base.h"
#include "dram/scheduler/entry.h"
#include "dram/state.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class DRAMChannel;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class ChannelLevelScheduler : public DRAMBaseScheduler
{
private:
    dram_rw_queue_type read_queue_;
    dram_rw_queue_type write_queue_;
public:
    ChannelLevelScheduler(DRAMChannel*, const DRAMChannelState&);

    cmd_output_type select_ready_command(void) override;
    /*
     * Useful inlines:
     * */
    inline bool can_accept(uint64_t address, TransactionType t) const override
    {
        if (trans_is_read(t))
            return read_queue_.size() < DRAM_RQ_SIZE;
        else
            return write_queue_.size() < DRAM_WQ_SIZE;
    }

    inline size_t read_occu(void) const override
    {
        return read_queue_.size();
    }

    inline size_t write_occu(void) const override
    {
        return write_queue_.size();
    }
private:
    inline bool add_to_rw_queue(Transaction trans) override
    {
        auto& q = trans_is_read(trans.type) ? read_queue_ : write_queue_;
        size_t s = trans_is_read(trans.type) ? DRAM_RQ_SIZE : DRAM_WQ_SIZE;
        if (q.size() < s)
        {
            q.push_back(trans);
            return true;
        }
        else
        {
            return false;
        }
    }

    inline bool any_write_queues_full(void) const override
    {
        return write_queue_.size() == DRAM_WQ_SIZE;
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_SCHEDULER_CHANNEL_LEVEL_h

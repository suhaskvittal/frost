/*
 *  author: Suhas Vittal
 *  date:   26 February 2025
 * */

#ifndef DRAM_ALT_SCHEDULER_h
#define DRAM_ALT_SCHEDULER_h

#include "dram/scheduler.h"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class AlternateDRAMScheduler
{
public:
    uint64_t s_write_forwards_ =0;

    const size_t low_watermark_;
    const size_t high_watermark_;
private:
    using pending_type = std::unordered_multiset<uint64_t>;
    using cmd_queue_array = std::array<dram_rw_queue_type, DRAM_TOT_BANKS_PER_CHANNEL>;

    dram_rw_queue_type read_queue_;
    dram_rw_queue_type write_queue_;
    pending_type pending_reads_;
    pending_type pending_writes_;

    size_t write_draining_ =0;
    
    cmd_queue_array cmd_queues_;
    size_t next_bank_idx_;

    DRAMChannel* owning_channel_;
    const DRAMChannelState& channel_state_;
public:
    using cmd_output_type = std::tuple<DRAMCommand, std::optional<RWQueueEntry>>;

    AlternateDRAMScheduler(DRAMChannel*, const DRAMChannelState&);

    bool            add_incoming(Transaction);
    cmd_output_type select_ready_command(void);
    void            update_state(void);

    inline void handle_preab_forced_transition(void) {}
    inline bool deadlock_find_inst(const inst_ptr) const { return true; }

    inline bool can_accept(const Transaction& trans) const
    {
        const auto& q = trans.is_read() ? read_queue_ : write_queue_;
        size_t s = trans.is_read() ? DRAM_RQ_SIZE : DRAM_WQ_SIZE;
        return q.size() < s;
    }

    inline size_t read_occu(void) const
    {
        return pending_reads_.size(); 
    }

    inline size_t write_occu(void) const 
    { 
        return pending_writes_.size();
    }

    inline bool is_in_write_mode(void) const { return false; }
    inline bool is_in_transition(void) const { return false; }

    inline bool any_write_queues_full() const
    {
        return write_queue_.size() == DRAM_WQ_SIZE;
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_ALT_SCHEDULER_h

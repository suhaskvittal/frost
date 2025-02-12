/*
 *  author: Suhas Vittal
 *  date:   11 February 2025
 * */

#ifndef DRAM_SCHEDULER_BASE_h
#define DRAM_SCHEDULER_BASE_h

#include "dram/command.h"
#include "dram/cmd_args.h"
#include "dram/scheduler/entry.h"
#include "dram/state.h"

#include <array>
#include <optional>
#include <tuple>
#include <unordered_set>
#include <vector>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class DRAMChannel;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct SchedulerState
{
    using issue_prio_array = std::array<int8_t, DRAM_TOT_BANKS_PER_CHANNEL>;

    issue_prio_array highest_priority;

    SchedulerState(void)
    {
        highest_priority.fill(std::numeric_limits<int8_t>::lowest());
    }

    inline void update_priority(size_t bank_idx, int8_t p)
    {
        int8_t& cp = highest_priority[bank_idx];
        cp = std::max(cp, p);
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class DRAMBaseScheduler
{
public:
    const size_t low_watermark_;
    const size_t high_watermark_;
protected:
    using pending_type = std::unordered_multiset<uint64_t>;
    using active_buffer_type = std::unordered_set<size_t>;
    /*
     * `owning_channel_` is just for stats:
     * */
    DRAMChannel* owning_channel_;
    const DRAMChannelState& channel_state_;
    /*
     * Managing read <--> write transitions:
     * */
    bool in_write_mode_ =false;
    bool in_transition_ =false;
    /*
     * `pending_reads_` and `pending_writes_` help with dependency enforcement and forwarding:
     * */
    pending_type pending_reads_;
    pending_type pending_writes_;

    size_t next_bank_idx_ =0;
    /*
     * `active_buffer_` stores indices with unused activates:
     * */
    active_buffer_type active_buffer_;
    /*
     * This is for specifically ensuring a uniform random distribution in `DRAM_RANDOMIZE_WRITE_ADDRESSES`:
     * */
    size_t dram_randomize_write_addresses_bank_idx_ =0;
public:
    using cmd_output_type = std::tuple<DRAMCommand, std::optional<RWQueueEntry>>;

    DRAMBaseScheduler(DRAMChannel*, const DRAMChannelState&);

    inline void update_state(void)
    {
        if (in_transition_)
            try_to_transition();
        else if (in_write_mode_)
            try_switch_to_reads();
        else
            try_switch_to_writes();
    }
    /*
     * IO functions:
     * */
    virtual bool can_accept(uint64_t, TransactionType) const =0;
    bool add_incoming(Transaction);
    
    virtual cmd_output_type select_ready_command(void) =0;
    /*
     * This should be called if the channel needs to issue a PREab to eventually do a REFab
     * */
    void handle_preab_forced_transition(void);
    /*
     * These return the number of pending reads/writes. Implementation specific:
     * */
    virtual size_t read_occu(void) const =0;
    virtual size_t write_occu(void) const =0;

    inline bool is_in_write_mode(void) const
    {
        return in_write_mode_;
    }

    inline bool is_in_transition(void) const
    {
        return in_transition_;
    }

    virtual bool deadlock_find_inst(const inst_ptr) const;
protected:
    using bank_cmd_type = std::tuple<DRAMCommand, dram_rw_queue_type*, dram_rw_queue_type::iterator>;
    using bank_cmd_array = std::vector<bank_cmd_type>;
    /*
     * RW queue insertion: implementation-specific
     * */
    virtual bool add_to_rw_queue(Transaction) =0;
    /*
     * Channel-level arbitration (which bank to accept from):
     * */
    cmd_output_type select_from_bank_commands(bank_cmd_array);
    /*
     * Bank-level arbitration (which command to prioritize/what to issue):
     * */
    bool allow_demand_precharge(
            dram_rw_queue_type::const_iterator q_it,
            dram_rw_queue_type::const_iterator end,
            const SchedulerState&,
            const DRAMBankState&);

    DRAMCommandType select_cas_command(
            dram_rw_queue_type::const_iterator q_it,
            dram_rw_queue_type::const_iterator end,
            const SchedulerState&,
            const DRAMBankState&);
    /*
     * Priority functions:
     * */
    virtual void try_switch_to_reads(void);
    virtual void try_switch_to_writes(void);

    virtual void try_to_transition(void);

    virtual bool any_write_queues_full(void) const =0;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_SCHEDULER_BASE_h

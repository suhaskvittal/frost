/*
 *  author: Suhas Vittal
 *  date:   12 February 2025
 * */

#ifndef DRAM_SCHEDULER_h
#define DRAM_SCHEDULER_h

#include "dram/address.h"
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

inline size_t dram_s_queue_index(uint64_t address)
{
    return fast_mod<DRAM_QUEUE_COUNT>(dram_bank_idx(address));
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class DRAMScheduler
{
public:
    const size_t low_watermark_;
    const size_t high_watermark_;
private:
    using queue_array = std::array<dram_rw_queue_type, DRAM_QUEUE_COUNT>;
    using pending_type = std::unordered_multiset<uint64_t>;
    using active_buffer_type = std::unordered_set<size_t>;
    using bank_counter_array = std::array<ssize_t, DRAM_TOT_BANKS_PER_CHANNEL>;
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
    
    bank_counter_array min_writes_per_bank_{};
    /*
     * `pending_reads_` and `pending_writes_` help with dependency enforcement and forwarding:
     * */
    queue_array  read_queues_{};
    queue_array  write_queues_{};
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

    DRAMScheduler(DRAMChannel*, const DRAMChannelState&);

    bool            add_incoming(Transaction);
    cmd_output_type select_ready_command(void);
    void            handle_preab_forced_transition(void);
    bool            deadlock_find_inst(const inst_ptr) const;
    /*
     * Useful inlines:
     * */
    inline void update_state(void)
    {
        if (in_write_mode_)
            try_switch_to_reads();
        else
            try_switch_to_writes();

        if (in_transition_)
            try_to_transition();
    }

    inline bool can_accept(uint64_t address, TransactionType t) const
    {
        size_t q_idx = dram_s_queue_index(address);
        const auto& q = trans_is_read(t) ? read_queues_.at(q_idx) : write_queues_.at(q_idx);
        size_t s = trans_is_read(t) ? DRAM_RQ_SIZE : DRAM_WQ_SIZE;
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

    inline bool is_in_write_mode(void) const { return in_write_mode_; }
    inline bool is_in_transition(void) const { return in_transition_; }

    inline bool any_write_queues_full() const
    {
        if constexpr (DRAM_QUEUE_COUNT == 1)
        {
            return write_occu() == DRAM_WQ_SIZE;
        }
        else
        {
            return std::any_of(write_queues_.begin(), write_queues_.end(),
                            [] (const auto& q) { return q.size() >= DRAM_WQ_SIZE; });
        }
    }
private:
    using bank_cmd_type = std::tuple<DRAMCommand, dram_rw_queue_type*, dram_rw_queue_type::iterator>;
    using bank_cmd_array = std::array<std::optional<bank_cmd_type>, DRAM_TOT_BANKS_PER_CHANNEL>;
    /*
     * Channel-level scheduling implementation:
     * */
    cmd_output_type select_from_bank_commands(bank_cmd_array&&);
    /*
     * Bank-level scheduling implementation:
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
    void try_switch_to_reads(void);
    void try_switch_to_writes(void);
    void try_to_transition(void);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_SCHEDULER_h

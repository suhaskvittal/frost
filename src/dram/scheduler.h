/*
 *  author: Suhas Vittal
 *  date:   31 December 2024
 * */

#ifndef DRAM_SCHEDULER_h
#define DRAM_SCHEDULER_h

#include "constants.h"

#include "dram/address.h"
#include "dram/command.h"
#include "dram/state.h"
#include "transaction.h"
#include "util/numerics.h"

#include <array>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <iosfwd>
#include <limits>
#include <tuple>
#include <optional>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct CmdQueueEntry
{
    Transaction     trans;
    DRAMCommandType type;
    bool is_row_buffer_hit =false;

    uint64_t cycle_entered_queue;

    CmdQueueEntry(Transaction t, DRAMCommandType c)
        :trans(t),
        type(c),
        cycle_entered_queue(GL_DRAM_CYCLE)
    {}
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * This is a simple extension of `std::deque` that counts the number of writes
 * in the queue
 * */
struct CmdQueue : public std::deque<CmdQueueEntry>
{
    size_t writes =0;

    inline void emplace_back(Transaction&& t, DRAMCommandType c)
    {
        if (cmd_is_write(c))
            ++writes;
        std::deque<CmdQueueEntry>::emplace_back(t, c);
    }

    inline size_t num_writes(void) const { return writes; }
    inline size_t num_reads(void) const { return size() - num_writes(); }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * Use this struct for any metadata needed for scheduling.
 * */
struct AlgoState
{
    bool is_first =true;

    const DRAMBankState& bank;

    AlgoState(const DRAMBankState& b)
        :bank(b)
    {}
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class CommandScheduler
{
public:
    uint64_t s_write_bursts_ =0;
    uint64_t s_max_writes_in_burst_ =0;
    uint64_t s_min_writes_in_burst_ =std::numeric_limits<uint64_t>::max();
private:
    using cmd_queue_array_type = std::array<CmdQueue, DRAM_TOT_BANKS_PER_CHANNEL>;
    using write_counter_array_type = std::array<size_t, DRAM_TOT_BANKS_PER_CHANNEL>;

    const DRAMChannelState& state_;
    /*
     * Command queues and pointer to next command queue to select from. Command queues
     * are selected in a round robin.
     * */
    cmd_queue_array_type cmd_queues_{};
    size_t next_cmd_queue_idx_ =0;
    /*
     * For scheduling policies that require write synchronization:
     * */
    bool global_write_mode_ =false;

    uint64_t write_burst_count_ =0;
public:
    using cmd_output_type = std::tuple<DRAMCommand, std::optional<CmdQueueEntry>>;

    CommandScheduler(const DRAMChannelState&);

    bool can_accept(uint64_t address, bool is_write) const;
    bool has_no_pending_reads(void) const;
    bool has_no_pending_writes(void) const;
    void enqueue(Transaction&&, DRAMCommandType);

    cmd_output_type select_command(void);

    void print_queue_state(std::ostream&) const;
private:
    cmd_output_type select_command_from_queue(CmdQueue&, const DRAMBankState&);

    bool skip_command(CmdQueue::const_iterator, const CmdQueue&, const AlgoState&);
    bool allow_demand_precharge(CmdQueue::const_iterator, const CmdQueue&, const AlgoState&);

    inline const DRAMBankState& get_bank_ref(size_t ii) const
    {
        size_t i = fast_mod<DRAM_BANKS>(ii),
               j = fast_mod<DRAM_BANKGROUPS>(ii >> numeric_traits<DRAM_BANKS>::log2),
               k = fast_mod<DRAM_RANKS>(ii >> numeric_traits<DRAM_BANKS*DRAM_BANKGROUPS>::log2);
        return state_.at(k).at(j).at(i);
    }
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_SCHEDULER_h

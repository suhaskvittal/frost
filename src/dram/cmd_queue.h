/*
 *  author: Suhas Vittal
 *  date:   24 December 2024
 * */

#ifndef DRAM_CMD_QUEUE_h
#define DRAM_CMD_QUEUE_h

#include "constants.h"

#include "dram/address.h"
#include "dram/command.h"
#include "dram/state.h"

#include <cstdint>
#include <deque>
#include <iosfwd>
#include <memory>
#include <type_traits>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class DRAMSchedPolicy
{
    FCFS,       // first come first serve
    FRFCFS,     // row-hits, then fcfs -- has demand precharge to ensure some fairness
};

enum class DRAMWritePolicy
{
    ASAP,       // Writes are finished in their command queue order.
    ALAP,       // Writes are only issued if `MAX_WRITES` is reached
    ALAP_SYNC   // Writes are issued if any command queue reaches `MAX_WRITES`
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct CmdQueueEntry
{
    Transaction     trans;
    DRAMCommandType type;
    bool is_row_buffer_hit =true;

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
 * `CmdQueue` contains the queuing structures for R/W commands. Commands are
 * completed according to `POL`.
 *
 * Note that each policy does not necessarily have a method dedicated to
 * its implementation. The following is assumed:
 *  (1) If the row is closed, then it must be activated. All policies will do this.
 *  (2) If the row is open, then it will either have a CAS or PRE. At the end of the
 *      day, most common scheduling policies dictate when a PRE occurs (i.e., FCFS or
 *      FRFCFS); otherwise, R/W are performed. Hence, `allow_precharge` (see below)
 *      is used to implement a scheduling algorithm.
 * 
 * We offer a parameter `ASSUME_BANK_SPECIFIC` to help streamline the common case
 * of command queues being per-bank. In this case, `bank_p_` will be used to
 * retrieve the appropriate bank-state during scheduling. The user should set
 * `bank_p_` some time before the use of the queue.
 * */
template <DRAMSchedPolicy SCHED_POL,
            size_t SIZE,
            bool ASSUME_BANK_SPECIFIC,
            // Optionals:
            DRAMWritePolicy WPOL=DRAMWritePolicy::ASAP>
class CmdQueue
{
public:
    constexpr static size_t ALAP_MAX_WRITES = SIZE / 4;

    const DRAMBankState* bank_p_ =nullptr;
private:
    using queue_t = std::deque<CmdQueueEntry>;

    queue_t impl_;
    size_t  writes_in_queue_ =0;
    size_t  writes_to_drain_ =0;
public:
    /*
     * If the first entry (the command) is a CAS command, then the second entry has a value.
     * */
    using cmd_output_t = std::tuple<DRAMCommand, std::optional<CmdQueueEntry>>;

    void enqueue(Transaction&&, DRAMCommandType);
    cmd_output_t select_command(const DRAMChannelState&, bool force_select_write=false);
    void print_queue_contents(std::ostream&);
    /*
     * Simple inline functions:
     * */
    inline bool can_accept(bool) const { return impl_.size() < SIZE; }
    inline bool has_no_pending_reads(void) const { return num_reads() == 0; }
    inline size_t size(void) const { return impl_.size(); }
    inline size_t num_reads(void) const { return impl_.size() - writes_in_queue_; }
    inline size_t num_writes(void) const { return writes_in_queue_; }

    inline void set_write_drain_count(size_t d) { writes_to_drain_ = d; }
private:
    const DRAMBankState& get_bank_ref(const DRAMChannelState&, uint64_t address);
    /*
     * Checks if the queue should switch to write mode. Used by WPOL = `ALAP` only.
     * */
    void alap_update_write_mode(void);
    /*
     * Bulk of scheduling policy implementation.
     * */
    bool allow_demand_precharge(const DRAMBankState&, bool is_first, queue_t::iterator, queue_t::iterator end);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

inline const DRAMBankState&
get_bank_state(const DRAMChannelState& ch, uint64_t address)
{
    size_t ra = dram_rank(address),
           bg = dram_bankgroup(address),
           ba = dram_bank(address);
    return ch.at(ra).at(bg).at(ba);
}

inline size_t 
get_bankgroup_idx(uint64_t address)
{
    return dram_bankgroup(address) + dram_rank(address)*DRAM_BANKGROUPS;
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "cmd_queue.tpp"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_CMD_QUEUE_h

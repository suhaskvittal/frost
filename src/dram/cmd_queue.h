/*
 *  author: Suhas Vittal
 *  date:   24 December 2024
 * */

#ifndef DRAM_CMD_QUEUE_h
#define DRAM_CMD_QUEUE_h

#include "globals.h"

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
 * of command queues being per-bank. In this case, `bank_idx` will be used to
 * retrieve the appropriate bank-state during scheduling. The user should set
 * `bank_idx_` some time before the use of the queue.
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

    using queue_t = std::deque<DRAMCommand>;

    const DRAMBankState* bank_p_ =nullptr;
private:

    queue_t impl_;
    size_t  writes_in_queue_ =0;
    size_t  writes_to_drain_ =0;
public:
    void enqueue(DRAMCommand&&);

    DRAMCommand select_command(const DRAMChannelState&, bool force_select_write=false);

    void print_queue_contents(std::ostream&);
    /*
     * Simple inline functions:
     * */
    inline bool can_accept(bool write) const { return impl_.size() < SIZE; }
    inline bool has_no_pending_reads(void) const { return num_reads() == 0; }
    inline size_t size(void) const { return impl_.size(); }
    inline size_t num_reads(void) const { return impl_.size() - writes_in_queue_; }
    inline size_t num_writes(void) const { return writes_in_queue_; }

    inline void set_write_drain_count(size_t d) { writes_to_drain_ = d; }
private:
    const DRAMBankState& get_bank_ref(const DRAMChannelState&, uint64_t address);

    bool allow_demand_precharge(const DRAMBankState&, bool is_first, queue_t::iterator, queue_t::iterator end);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * This is just a wrapper for the entire command queueing structure.
 * */
class CommandScheduler
{
public:
private:
    constexpr static size_t TOT_BANKS = DRAM_RANKS*DRAM_BANKGROUPS*DRAM_BANKS;
    constexpr static DRAMSchedPolicy SCHED_POLICY = DRAMSchedPolicy::FRFCFS;
#if defined(DRAM_USE_ALAP_SYNC)
    constexpr static DRAMWritePolicy WRITE_POLICY = DRAMWritePolicy::ALAP_SYNC;
#elif defined(DRAM_USE_ALAP)
    constexpr static DRAMWritePolicy WRITE_POLICY = DRAMWritePolicy::ALAP;
#else
    constexpr static DRAMWritePolicy WRITE_POLICY = DRAMWritePolicy::ASAP;
#endif
    /*
     * Command queue definitions:
     * */
    using cmd_queue_t = CmdQueue<
                                SCHED_POLICY,
                                DRAM_CMDQ_SIZE,
                                true,
                                WRITE_POLICY>;
    using cmd_array_t = std::array<cmd_queue_t, TOT_BANKS>;

    const DRAMChannelState& state_;
    /*
     * Command queues and pointer to next command queue to select from. Command queues
     * are selected in a round robin.
     * */
    cmd_array_t cmd_queues_{};
    size_t next_cmd_queue_idx_ =0;
    /*
     * Used if write policy is ALAP_SYNC.
     * */
    using alap_sync_write_tracker_t = std::array<size_t, TOT_BANKS>;

    bool                      alap_sync_in_write_mode_ =false;
    alap_sync_write_tracker_t alap_sync_write_tracker_{};
public:
    CommandScheduler(const DRAMChannelState&);

    bool can_accept(uint64_t address, bool is_write);
    bool has_no_pending_reads(void) const;
    void enqueue(DRAMCommand&&);

    DRAMCommand select_command(void);

    void print_queue_state(std::ostream&);
private:
    void alap_sync_enter_write_mode(void);
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

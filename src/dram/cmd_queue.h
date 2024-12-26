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
#include <memory>
#include <type_traits>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class DRAMSchedPolicy {
    FCFS,       // first come first serve
    FRFCFS,     // row-hits, then fcfs -- has demand precharge to ensure some fairness
    FRRFCFS,    // FRFCFS, but write-hits are before read-hits
    ARFCFS      // any read, first come first serve -- any read goes before any write (in FRFCFS order)
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
template <DRAMSchedPolicy POL, bool ASSUME_BANK_SPECIFIC, bool QUEUE_IS_SPLIT=false>
class CmdQueue
{
public:
    using queue_t = std::deque<DRAMCommand>;

    const DRAMBankState* bank_p_ =nullptr;
private:
    constexpr static size_t RQ_SIZE = (DRAM_CMDQ_SIZE * 3)/4;
    constexpr static size_t WQ_SIZE = DRAM_CMDQ_SIZE - RQ_SIZE;

    using unified_impl = queue_t;
    struct split_impl
    {
        queue_t reads;
        queue_t writes;
        size_t writes_to_drain =0;
    };

    using queue_impl = typename std::conditional<QUEUE_IS_SPLIT, split_impl, unified_impl>::type;

    queue_impl impl_;
public:
    bool can_accept(bool write) const;
    bool has_no_pending_reads(void) const;
    void enqueue(DRAMCommand&&);

    DRAMCommand select_command(const DRAMChannelState&);
private:
    queue_t&             get_queue_ref(void);
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

    using per_bank_queue_t = CmdQueue<DRAMSchedPolicy::FRFCFS, true>;
    using per_bank_array_t = std::array<per_bank_queue_t, TOT_BANKS>;

    per_bank_array_t per_bank_queues_{};
    const DRAMChannelState& state_;

    size_t next_cmd_queue_idx_ =0;
public:
    CommandScheduler(const DRAMChannelState&);

    bool can_accept(uint64_t address, bool is_write) const;
    bool has_no_pending_reads(void) const;

    void enqueue(DRAMCommand&&);

    DRAMCommand select_command(void);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "cmd_queue.tpp"
#include "cmd_queue.inl"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_CMD_QUEUE_h

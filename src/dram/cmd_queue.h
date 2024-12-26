/*
 *  author: Suhas Vittal
 *  date:   24 December 2024
 * */

#ifndef DRAM_CMD_QUEUE_h
#define DRAM_CMD_QUEUE_h

#include "globals.h"

#include "dram/address.h"
#include "dram/command.h"

#include <cstdint>
#include <deque>
#include <memory>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

struct DRAMBankState;
struct DRAMChannelState;

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

enum class DRAMCmdQueuePolicy {
    FCFS,       // first come first serve
    FRFCFS,     // row-hits, then fcfs -- has demand precharge to ensure some fairness
    FRRFCFS,    // FRFCFS, but write-hits are before read-hits
    ARFCFS      // any read, first come first serve -- any read goes before any write (in FRFCFS order)
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/*
 * `CmdQueue` contains the queuing structures for R/W commands. Commands are
 * completed according to `DRAM_CMDQ_POLICY`.
 *
 * Note that each policy does not necessarily have a method dedicated to
 * its implementation. The following is assumed:
 *  (1) If the row is closed, then it must be activated. All policies will do this.
 *  (2) If the row is open, then it will either have a CAS or PRE. At the end of the
 *      day, most common scheduling policies dictate when a PRE occurs (i.e., FCFS or
 *      FRFCFS); otherwise, R/W are performed. Hence, `allow_precharge` (see below)
 *      is used to implement a scheduling algorithm.
 *
 * Any other specifics (i.e., as in FRRFCFS, write row hits are not allowed if read
 * row hits are available) can be implemented in `select_command`.
 * */
class CmdQueue
{
public:
private:
    using queue_t = std::deque<DRAMCommand>;
    /*
     * `impl_` does not have a well-defined implementation. It depends on how the
     * queue is designed. By default, we assume the queue is unified.
     *
     * It is expected that any scheduling policy works with `queue_t`
     * plus any metadata. So, scheduling need not know the command queue
     * structure.
     * */
#if defined(DRAM_CMDQ_SPLIT)
    struct 
    {
        queue_t reads;
        queue_t writes;
        size_t writes_to_drain =0;
    } impl_;
#else
    queue_t impl_;
#endif
public:
    /*
     * Since the implementation of the command queue can be changed, `can_accept` is a
     * catch-all function for seeing if a read/write can be enqueued.
     * */
    bool can_accept(bool is_write) const;
    bool has_no_pending_reads(void) const;
    void enqueue(DRAMCommand&&);

    DRAMCommand select_command(const DRAMChannelState&);
private:
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

    using cmd_queue_array_t = std::array<CmdQueue, TOT_BANKS>;

    cmd_queue_array_t cmd_queues_{};
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

using cmdq_iterator = std::deque<DRAMCommand>::iterator;

bool                 allow_precharge(const DRAMBankState&, bool is_first, cmdq_iterator next_begin, cmdq_iterator end);
const DRAMBankState& get_bank_state(const DRAMChannelState&, uint64_t address);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_CMD_QUEUE_h

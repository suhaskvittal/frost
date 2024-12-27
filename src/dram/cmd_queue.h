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
template <DRAMSchedPolicy POL, size_t SIZE, bool ASSUME_BANK_SPECIFIC, bool QUEUE_IS_SPLIT=false>
class CmdQueue
{
public:
    using queue_t = std::deque<DRAMCommand>;

    const DRAMBankState* bank_p_ =nullptr;
    constexpr static size_t RQ_SIZE = (SIZE * 3)/4;
    constexpr static size_t WQ_SIZE = SIZE - RQ_SIZE;

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
    size_t size(void) const;

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
    /*
     * ONLY PRINTED IF `DRAM_ENABLE_BG_WRITE_SYNC` ENABLED
     * */
    uint64_t s_tot_bg_sync_writes_ =0;
    uint64_t s_tot_bg_sync_drain_spread_ =0;
    uint64_t s_bg_sync_drains_ =0;
private:
    constexpr static size_t TOT_BANKS = DRAM_RANKS*DRAM_BANKGROUPS*DRAM_BANKS;
    /*
     * Command queue definitions:
     * */
    using per_bank_queue_t = CmdQueue<DRAMSchedPolicy::FRFCFS, DRAM_CMDQ_SIZE, true>;
    using per_bank_array_t = std::array<per_bank_queue_t, TOT_BANKS>;

    const DRAMChannelState& state_;
    /*
     * Command queues and pointer to next command queue to select from. Command queues
     * are selected in a round robin.
     * */
    per_bank_array_t per_bank_queues_{};
    size_t next_cmd_queue_idx_ =0;
    /*
     * `bg_write_array_t` is only used if `DRAM_ENABLE_BG_WRITE_SYNC`. If it is used,
     * then writes are redirected to `bg_write_queues`, and are completed when
     * any of the queues become full.
     * */
    constexpr static size_t TOT_BANKGROUPS = DRAM_RANKS*DRAM_BANKGROUPS;
    constexpr static size_t BG_WRITE_QUEUE_SIZE = (DRAM_CMDQ_SIZE / 3) * DRAM_BANKS;

    using bg_write_queue_t = CmdQueue<DRAMSchedPolicy::FRFCFS, BG_WRITE_QUEUE_SIZE, false>;
    using bg_write_array_t = std::array<bg_write_queue_t, TOT_BANKGROUPS>;

    bg_write_array_t bg_write_queues_{};
    size_t bg_drain_idx_ =0;
    bool   bg_write_mode_ =false;
    size_t bg_num_writes_ =0;
public:
    CommandScheduler(const DRAMChannelState&);

    bool can_accept(uint64_t address, bool is_write);
    bool has_no_pending_reads(void) const;

    void enqueue(DRAMCommand&&);

    DRAMCommand select_command(void);
private:
    DRAMCommand bg_sync_select_write(void);
    void bg_sync_init(size_t);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

const DRAMBankState& get_bank_state(const DRAMChannelState&, uint64_t address);
size_t               get_bankgroup_idx(uint64_t address);

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#include "cmd_queue.tpp"
#include "cmd_queue.inl"

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_CMD_QUEUE_h

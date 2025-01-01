/*
 *  author: Suhas Vittal
 *  date:   31 December 2024
 * */

#ifndef DRAM_SCHEDULER_h
#define DRAM_SCHEDULER_h

#include "dram/cmd_queue.h"

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
    void enqueue(Transaction&&, DRAMCommandType);

    cmd_queue_t::cmd_output_t select_command(void);

    void print_queue_state(std::ostream&);
private:
    void alap_sync_update_write_mode(void);
    void alap_sync_enter_write_mode(void);
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_SCHEDULER_h

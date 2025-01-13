/*
 *  author: Suhas Vittal
 *  date:   4 December 2024
 * */

#ifndef DRAM_CHANNEL_h
#define DRAM_CHANNEL_h

#include "constants.h"

#include "dram/command.h"
#include "dram/enums.h"
#include "dram/scheduler.h"
#include "dram/state.h"
#include "io_bus.h"
#include "transaction.h"

#include <array>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <optional>

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

class DRAMChannel
{
public:
    using in_queue_type = IOBus::in_queue_type;
    using pending_type = IOBus::pending_type;
    using out_queue_type = IOBus::out_queue_type;

    out_queue_type outgoing_queue_;

    uint64_t s_reads_ =0;
    uint64_t s_writes_ =0;
    uint64_t s_precharges_ =0;
    uint64_t s_activates_ =0;
    uint64_t s_refreshes_ =0;

    uint64_t s_pre_demand_ =0;

    uint64_t s_read_row_hits_ =0;
    uint64_t s_write_row_hits_ =0;

    uint64_t s_tot_read_latency_ =0;
    uint64_t s_tot_write_latency_ =0;

    uint64_t s_num_drains_ =0;
    /*
     * BELOW STATS ARE ONLY UPDATED AND PRINTED IF `DRAM_TRACK_ADVANCED_STATS` IS DEFINED.
     *  these are stats that are computationally intensive to compute, and thus can be disabled.
     * */
    using wrw_stat_type = std::array<uint64_t, 4>;

    wrw_stat_type s_num_seq_{};
    double s_tot_write_variance_ =0.0;

    const double freq_ghz_;
    const size_t channel_id_;
    const size_t low_watermark_;
    const size_t high_watermark_;
private:
    using write_drain_array_type = std::array<size_t, DRAM_TOT_BANKS_PER_CHANNEL>;
    using cmd_sch_ptr = std::unique_ptr<CommandScheduler>;
    /* 
     * Custom IO implementation
     * */
    in_queue_type read_queue_;
    in_queue_type write_queue_;
    pending_type pending_reads_;
    pending_type pending_writes_;
    /*
     * `writes_to_drain_` holds the maximum number of writes that can be issued from the write queue
     * for each bank.
     * */
    write_drain_array_type writes_to_drain_per_bank_{};
    size_t tot_writes_to_drain_ =0;

    DRAMChannelState  state_{};
    cmd_sch_ptr cmd_scheduler_;
    /*
     * Variables for tracking WR+W sequences:
     * */
    enum class WRWSequenceState { NEED_WRITE, NEED_READ, IN_READS };

    WRWSequenceState wrw_seq_state_ =WRWSequenceState::NEED_WRITE;
    uint64_t         wrw_first_write_cycle_ =0;
    /*
     * All variables below are used if `DRAM_ENABLE_LOGGER` is defined.
     * 
     * `dram_logger_` is used to log information and write to a file. File is default: `dram_channel.<id>.log`.
     *
     * `tmp_logger_` is used to buffer log info. `tmp_logger_` is only used to write to `dram_logger_` if a
     * command is issued.
     * */
    std::ofstream     dram_logger_{};
    std::stringstream tmp_logger_;
public:
    DRAMChannel(size_t channel_id, double freq_ghz);
    
    void tick_mc(void);
    void tick_dram(void);

    bool add_incoming(Transaction);

    inline size_t read_queue_size(void) const { return read_queue_.size(); }
    inline size_t write_queue_size(void) const { return write_queue_.size(); }
private:
    /*
     * Updates `writes_to_drain_` depending on the size of the write queue.
     * */
    void try_switch_to_write_mode(void);
    void issue_next_cmd(void);

    void update_wrw_state(const DRAMCommand&);
    /*
     * Directs the logger to only log write-read+-write sequences. Manages
     * and updates the state of the logger in this mode.
     * */
    void log_write_read_write_sequence(const DRAMCommand&);

    friend class DRAM;
};

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

#endif  // DRAM_CHANNEL_h
